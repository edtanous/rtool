#include "http_client.hpp"

#include <openssl/err.h>

#include <boost/asio/connect.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/basic_endpoint.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/flat_static_buffer.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/beast/version.hpp>
#include <boost/container/devector.hpp>
#include <boost/system/error_code.hpp>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <queue>
#include <string>
#include <format>

#include "boost_formatter.hpp"
#include "http_response.hpp"

namespace http {

namespace {
std::string printOsslError(const boost::system::error_code& ec) {
  std::string err = ec.message();
  if (ec.category() == boost::asio::error::get_ssl_category()) {
    err +=
        std::format(" ({},{}) ", ERR_GET_LIB(ec.value()),
                     ERR_GET_REASON(ec.value()));
    // ERR_PACK /* crypto/err/err.h */
    std::array<char, 128> buf;
    ::ERR_error_string_n(ec.value(), buf.data(), buf.size());
    err += buf.data();
  }
  return err;
}
}  // namespace

void ConnectionInfo::DoResolve() {
  SPDLOG_DEBUG("starting resolve");
  resolver_.async_resolve(
      host_, std::to_string(port_),
      std::bind_front(&ConnectionInfo::AfterResolve, this, shared_from_this()));
}

void ConnectionInfo::AfterResolve(
    const std::shared_ptr<ConnectionInfo>& /*self*/,
    const boost::system::error_code ec,
    const boost::asio::ip::tcp::resolver::results_type& endpoint_list) {
  if (ec || (endpoint_list.empty())) {
    return;
  }

  timer_.expires_after(std::chrono::seconds(30));
  timer_.async_wait(std::bind_front(OnTimeout, weak_from_this()));

  SPDLOG_DEBUG("starting connect");
  boost::asio::async_connect(
      conn_, endpoint_list,
      std::bind_front(&ConnectionInfo::AfterConnect, this, shared_from_this()));
}

void ConnectionInfo::AfterConnect(
    const std::shared_ptr<ConnectionInfo>& /*self*/,
    boost::beast::error_code ec,
    const boost::asio::ip::tcp::endpoint& /*endpoint*/) {
  timer_.cancel();
  if (ec) {
    SPDLOG_DEBUG("Connect failed: {}", ec);
    return;
  }
  SPDLOG_DEBUG("Connected");
  if (sslConn_) {
    DoSslHandshake();
    return;
  }
  SendMessage();
}

void ConnectionInfo::DoSslHandshake() {
  if (!sslConn_) {
    return;
  }
  timer_.expires_after(std::chrono::seconds(10));
  timer_.async_wait(std::bind_front(OnTimeout, weak_from_this()));

  SPDLOG_DEBUG("starting handshake");
  sslConn_->async_handshake(boost::asio::ssl::stream_base::client,
                            std::bind_front(&ConnectionInfo::AfterSslHandshake,
                                            this, shared_from_this()));
}

void ConnectionInfo::AfterSslHandshake(
    const std::shared_ptr<ConnectionInfo>& /*self*/,
    boost::beast::error_code ec) {
  timer_.cancel();
  if (ec) {
    SPDLOG_DEBUG("handshake failed {}", printOsslError(ec));
    return;
  }
  SPDLOG_DEBUG("handshake succeeded {}", ec);
  SendMessage();
}

void ConnectionInfo::SendMessage() {
  SPDLOG_DEBUG("getting message");
  channel_->async_receive(std::bind_front(&ConnectionInfo::OnMessageReadyToSend,
                                          this, shared_from_this()));

  conn_.async_wait(
      boost::asio::ip::tcp::socket::wait_error,
      std::bind_front(&ConnectionInfo::OnIdleEvent, this, weak_from_this()));
}

void ConnectionInfo::OnMessageReadyToSend(
    const std::shared_ptr<ConnectionInfo>& /*self*/,
    boost::system::error_code ec, PendingRequest pending) {
  if (ec) {
    if (ec == boost::asio::experimental::error::channel_cancelled){
      SPDLOG_DEBUG("Channel destroyed, closing connection {}", ec);
      return;
    }
    SPDLOG_ERROR("Failed to get message {}", ec);
    return;
  }
  SPDLOG_DEBUG("Got Message");

  // Cancel our idle waiting event
  conn_.cancel(ec);
  // intentionally ignore errors here.  It's possible there was
  // nothing in progress to cancel

  req_ = std::move(pending.req);
  callback_ = std::move(pending.callback);

  // Set a timeout on the operation
  timer_.expires_after(std::chrono::seconds(30));
  timer_.async_wait(std::bind_front(OnTimeout, weak_from_this()));

  // Send the HTTP request to the remote host
  if (sslConn_) {
    boost::beast::http::async_write(
        *sslConn_, *req_,
        std::bind_front(&ConnectionInfo::AfterWrite, this, shared_from_this()));
  } else {
    boost::beast::http::async_write(
        conn_, *req_,
        std::bind_front(&ConnectionInfo::AfterWrite, this, shared_from_this()));
  }
}

void ConnectionInfo::OnIdleEvent(const std::weak_ptr<ConnectionInfo>& /*self*/,
                                 const boost::system::error_code& ec) {
  if (ec && ec != boost::asio::error::operation_aborted) {
    DoClose();
  }
}

void ConnectionInfo::AfterWrite(const std::shared_ptr<ConnectionInfo>& /*self*/,
                                const boost::beast::error_code& ec,
                                size_t /*bytesTransferred*/) {
  timer_.cancel();
  if (ec) {
    callback_(Response(parser_->release()));
    return;
  }

  RecvMessage();
}

void ConnectionInfo::RecvMessage() {
  parser_.emplace(std::piecewise_construct, std::make_tuple());
  parser_->body_limit(kHttpReadBodyLimit);

  timer_.expires_after(std::chrono::seconds(30));
  timer_.async_wait(std::bind_front(OnTimeout, weak_from_this()));

  // Receive the HTTP response
  if (sslConn_) {
    boost::beast::http::async_read(
        *sslConn_, buffer_, *parser_,
        std::bind_front(&ConnectionInfo::AfterRead, this, shared_from_this()));
  } else {
    boost::beast::http::async_read(
        conn_, buffer_, *parser_,
        std::bind_front(&ConnectionInfo::AfterRead, this, shared_from_this()));
  }
}

void ConnectionInfo::AfterRead(const std::shared_ptr<ConnectionInfo>& /*self*/,
                               const boost::beast::error_code& ec,
                               const std::size_t bytesTransferred) {
  SPDLOG_DEBUG("Read {} from server ec={}", bytesTransferred, ec);
  timer_.cancel();
  if (ec && ec != boost::asio::ssl::error::stream_truncated) {
    callback_(Response(parser_->release()));
    return;
  }
  // Keep the connection alive if server supports it
  // Else close the connection

  // Copy the response into a Response object so that it can be
  // processed by the callback function.
  bool keep_alive = parser_->get().keep_alive();
  callback_(Response(parser_->release()));

  // Callback has served its purpose, let it destruct
  callback_ = nullptr;

  // Is more data is now loaded for the next request?
  if (keep_alive) {
    SendMessage();
  } else {
    // Server is not keep-alive enabled so we need to close the
    // connection and then start over from resolve
    DoClose();
    DoResolve();
  }
}

void ConnectionInfo::OnTimeout(const std::weak_ptr<ConnectionInfo>& weak_self,
                               const boost::system::error_code ec) {
  if (ec == boost::asio::error::operation_aborted) {
    return;
  }
  if (ec) {
    // If the timer fails, we need to close the socket anyway, same
    // as if it expired.
  }
  std::shared_ptr<ConnectionInfo> self = weak_self.lock();
  if (self == nullptr) {
    return;
  }
  self->DoClose();
}

void ConnectionInfo::OnTimerDone(
    const std::shared_ptr<ConnectionInfo>& /*self*/,
    const boost::system::error_code& ec) {
  if (ec == boost::asio::error::operation_aborted) {
    return;
  }
  if (ec) {
  }

  // Let's close the connection
  DoClose();
}

void ConnectionInfo::ShutdownConn() {
  channel_->cancel();
  boost::beast::error_code ec;
  conn_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
  conn_.close();
}

void ConnectionInfo::DoClose() {
  if (!sslConn_) {
    ShutdownConn();
    return;
  }

  sslConn_->async_shutdown(std::bind_front(&ConnectionInfo::AfterSslShutdown,
                                           this, shared_from_this()));
}

void ConnectionInfo::AfterSslShutdown(
    const std::shared_ptr<ConnectionInfo>& /*self*/,
    const boost::system::error_code& /*ec*/) {
  ShutdownConn();
}

void ConnectionInfo::SetCipherSuiteTlSext() {
  if (!sslConn_) {
    return;
  }
  // NOTE: The SSL_set_tlsext_host_name is defined in tlsv1.h header
  // file but its having old style casting (name is cast to void*).
  // Since bmcweb compiler treats all old-style-cast as error, its
  // causing the build failure. So replaced the same macro inline and
  // did corrected the code by doing static_cast to viod*. This has to
  // be fixed in openssl library in long run. Set SNI Hostname (many
  // hosts need this to handshake successfully)
  if (SSL_ctrl(sslConn_->native_handle(), SSL_CTRL_SET_TLSEXT_HOSTNAME,
               TLSEXT_NAMETYPE_host_name,
               static_cast<void*>(&host_.front())) == 0)

  {
    boost::beast::error_code ec{static_cast<int>(::ERR_get_error()),
                                boost::asio::error::get_ssl_category()};

    return;
  }
}

ConnectionInfo::ConnectionInfo(boost::asio::io_context& ioc_in,
                               const std::string& dest_ip_in,
                               uint16_t dest_port_in,
                               const std::shared_ptr<ConnectPolicy>& policy_in,
                               const std::shared_ptr<Channel>& channel_in)
    : host_(dest_ip_in),
      port_(dest_port_in),
      resolver_(ioc_in),
      conn_(ioc_in),
      policy_(policy_in),
      timer_(ioc_in),
      channel_(channel_in) {
  SPDLOG_DEBUG("Constructing ConnectionInfo");
  if (policy_->use_tls) {
    boost::asio::ssl::context ssl_ctx(boost::asio::ssl::context::tls_client);

    boost::system::error_code ec;

    // Support only TLS v1.2 & v1.3
    ssl_ctx.set_options(boost::asio::ssl::context::default_workarounds |
                            boost::asio::ssl::context::no_sslv2 |
                            boost::asio::ssl::context::no_sslv3 |
                            boost::asio::ssl::context::single_dh_use |
                            boost::asio::ssl::context::no_tlsv1 |
                            boost::asio::ssl::context::no_tlsv1_1,
                        ec);
    if (ec) {
      std::cout << "Error";
      return;
    }

    if (policy_->verify_server_certificate) {
      // Add a directory containing certificate authority files to be
      // used for performing verification.
      ssl_ctx.set_default_verify_paths(ec);
      if (ec) {
        std::cout << "Error";
        return;
      }

      // Verify the remote server's certificate
      ssl_ctx.set_verify_mode(boost::asio::ssl::verify_peer, ec);
      if (ec) {
        std::cout << "Error";
        return;
      }
    } else {
      ssl_ctx.set_verify_mode(boost::asio::ssl::verify_none, ec);
      if (ec) {
        std::cout << "Error";
        return;
      }
    }

    // All cipher suites are set as per OWASP datasheet.
    // https://cheatsheetseries.owasp.org/cheatsheets/TLS_Cipher_String_Cheat_Sheet.html
    constexpr const char* kSslCiphers =
        "ECDHE-ECDSA-AES128-GCM-SHA256:"
        "ECDHE-RSA-AES128-GCM-SHA256:"
        "ECDHE-ECDSA-AES256-GCM-SHA384:"
        "ECDHE-RSA-AES256-GCM-SHA384:"
        "ECDHE-ECDSA-CHACHA20-POLY1305:"
        "ECDHE-RSA-CHACHA20-POLY1305:"
        "DHE-RSA-AES128-GCM-SHA256:"
        "DHE-RSA-AES256-GCM-SHA384"
        "TLS_AES_128_GCM_SHA256:"
        "TLS_AES_256_GCM_SHA384:"
        "TLS_CHACHA20_POLY1305_SHA256";

    if (SSL_CTX_set_cipher_list(ssl_ctx.native_handle(), kSslCiphers) != 1) {
      std::cout << "Error";
      return;
    }

    sslConn_.emplace(conn_, ssl_ctx);
    SetCipherSuiteTlSext();
  }
}

void ConnectionInfo::Start() { DoResolve(); }

void ConnectionPool::QueuePending(PendingRequest&& pending) {
  // If we have to queue it, push it into the request queue in time
  // order
  if (pushInProgress_) {
    if (requestQueue_.size() >= kMaxRequestQueueSize) {
      return;
    }
    requestQueue_.emplace_back(std::move(pending));
    return;
  }

  // Make sure we have some connections open ready to receive
  for (std::weak_ptr<ConnectionInfo>& weak_conn : connections_) {
    std::shared_ptr<ConnectionInfo> conn = weak_conn.lock();
    if (conn != nullptr) {
      continue;
    }

    conn = std::make_shared<ConnectionInfo>(ioc_, destIP_, destPort_, policy_,
                                            channel_);
    conn->Start();
    weak_conn = conn->weak_from_this();

    // Only need to construct one extra connection max
    break;
  }
  pushInProgress_ = true;

  SPDLOG_DEBUG("sending");

  channel_->async_send(
      boost::system::error_code(), std::move(pending),
      std::bind_front(&ConnectionPool::ChannelPushComplete, weak_from_this()));
}

void ConnectionPool::ChannelPushComplete(
    const std::weak_ptr<ConnectionPool>& weak_self,
    boost::system::error_code ec) {
  SPDLOG_DEBUG("Channel Push complete");
  std::shared_ptr<ConnectionPool> self = weak_self.lock();
  if (self == nullptr) {
    return;
  }

  self->pushInProgress_ = false;
  if (ec) {
    SPDLOG_ERROR("Channel Push failed: {}", ec);
    return;
  }

  if (!self->requestQueue_.empty()) {
    self->pushInProgress_ = true;

    SPDLOG_DEBUG("sending");

    self->channel_->async_send(
        boost::system::error_code(), std::move(self->requestQueue_.front()),
        std::bind_front(&ConnectionPool::ChannelPushComplete,
                        self->weak_from_this()));
    self->requestQueue_.pop_front();
  }
}

ConnectionPool::ConnectionPool(boost::asio::io_context& ioc_in,
                               std::string_view dest_ip_in,
                               uint16_t dest_port_in,
                               const std::shared_ptr<ConnectPolicy>& policy_in)
    : ioc_(ioc_in),
      destIP_(dest_ip_in),
      destPort_(dest_port_in),
      policy_(policy_in),
      channel_(std::make_shared<Channel>(ioc_, 128)) {}

Client::Client(boost::asio::io_context& ioc_in, ConnectPolicy policy_in)
    : policy_(std::make_shared<ConnectPolicy>(policy_in)), ioc_(ioc_in) {}

// Send request to destIP:destPort and use the provided callback to
// handle the response
void Client::SendData(std::string&& data, std::string_view dest_ip,
                      uint16_t dest_port, std::string_view dest_uri,
                      const boost::beast::http::fields& http_header,
                      const boost::beast::http::verb verb,
                      const std::function<void(Response&&)>& res_handler) {
  std::string client_key = policy_->use_tls ? "https" : "http";
  client_key += dest_ip;
  client_key += ":";
  client_key += std::to_string(dest_port);
  SPDLOG_DEBUG("Requesting {}{}", client_key, dest_uri);
  // Use nullptr to avoid creating a ConnectionPool each time
  std::shared_ptr<ConnectionPool>& conn = connectionPools_[client_key];
  if (conn == nullptr) {
    // Now actually create the ConnectionPool shared_ptr since it
    // does not already exist
    conn = std::make_shared<ConnectionPool>(ioc_, dest_ip, dest_port, policy_);
  }

  // Send the data using either the existing connection pool or the
  // newly created connection pool Construct the request to be sent
  boost::beast::http::request<boost::beast::http::string_body> this_req(
      verb, dest_uri, 11, "", http_header);
  this_req.set(boost::beast::http::field::host, dest_ip);
  this_req.keep_alive(true);
  this_req.body() = std::move(data);
  this_req.prepare_payload();
  conn->QueuePending(PendingRequest(std::move(this_req), res_handler));
}
}  // namespace http
