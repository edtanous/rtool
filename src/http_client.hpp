#pragma once
#include <boost/asio/connect.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
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
#include <fmt/core.h>
#include <fmt/format.h>

#include "http_response.hpp"

namespace http {

// It is assumed that the BMC should be able to handle 4 parallel
// connections
constexpr uint8_t kMaxPoolSize = 4;
constexpr uint8_t kMaxRequestQueueSize = 50;
constexpr unsigned int kHttpReadBodyLimit = 131072;
constexpr unsigned int kHttpReadBufferSize = 4096;

struct PendingRequest {
  boost::beast::http::request<boost::beast::http::string_body> req;
  std::function<void(Response&&)> callback;
  PendingRequest(
      boost::beast::http::request<boost::beast::http::string_body>&& req_in,
      const std::function<void(Response&&)>& callback_in)
      : req(std::move(req_in)), callback(callback_in) {}
  PendingRequest() = default;
};

using Channel = boost::asio::experimental::concurrent_channel<void(
    boost::system::error_code, PendingRequest)>;

struct ConnectPolicy {
  bool verify_server_certificate = true;
  bool use_tls = true;
};

class ConnectionInfo : public std::enable_shared_from_this<ConnectionInfo> {
 private:
  std::string host_;
  uint16_t port_;

  // Data buffers
  using BodyType = boost::beast::http::string_body;
  using RequestType = boost::beast::http::request<BodyType>;
  std::optional<RequestType> req_;
  std::optional<boost::beast::http::response_parser<BodyType> > parser_;
  boost::beast::flat_static_buffer<kHttpReadBufferSize> buffer_;

  // Async callables
  std::function<void(Response&&)> callback_;
  boost::asio::ip::tcp::resolver resolver_;
  boost::asio::ip::tcp::socket conn_;
  std::shared_ptr<ConnectPolicy> policy_;
  std::optional<boost::beast::ssl_stream<boost::asio::ip::tcp::socket&> >
      sslConn_;

  boost::asio::steady_timer timer_;

  std::shared_ptr<Channel> channel_;

  friend class ConnectionPool;

  void DoResolve();

  void AfterResolve(
      const std::shared_ptr<ConnectionInfo>& /*self*/,
      boost::system::error_code ec,
      const boost::asio::ip::tcp::resolver::results_type& endpoint_list);

  void AfterConnect(const std::shared_ptr<ConnectionInfo>& /*self*/,
                    boost::beast::error_code ec,
                    const boost::asio::ip::tcp::endpoint& /*endpoint*/);

  void DoSslHandshake();

  void AfterSslHandshake(const std::shared_ptr<ConnectionInfo>& /*self*/,
                         boost::beast::error_code ec);

  void SendMessage();

  void OnMessageReadyToSend(const std::shared_ptr<ConnectionInfo>& /*self*/,
                            boost::system::error_code ec,
                            PendingRequest pending);

  void OnIdleEvent(const std::weak_ptr<ConnectionInfo>& /*self*/,
                   const boost::system::error_code& ec);

  void AfterWrite(const std::shared_ptr<ConnectionInfo>& /*self*/,
                  const boost::beast::error_code& ec,
                  size_t /*bytesTransferred*/);

  void RecvMessage();

  void AfterRead(const std::shared_ptr<ConnectionInfo>& /*self*/,
                 const boost::beast::error_code& ec,
                 std::size_t /*bytesTransferred*/);

  static void OnTimeout(const std::weak_ptr<ConnectionInfo>& weak_self,
                        boost::system::error_code ec);

  void OnTimerDone(const std::shared_ptr<ConnectionInfo>& /*self*/,
                   const boost::system::error_code& ec);

  void ShutdownConn();

  void DoClose();

  void AfterSslShutdown(const std::shared_ptr<ConnectionInfo>& /*self*/,
                        const boost::system::error_code& ec);
  void SetCipherSuiteTlSext();

 public:
  explicit ConnectionInfo(boost::asio::io_context& ioc_in,
                          const std::string& dest_ip_in, uint16_t dest_port_in,
                          const std::shared_ptr<ConnectPolicy>& policy,
                          const std::shared_ptr<Channel>& channel_in);
  void Start();
};

class ConnectionPool : public std::enable_shared_from_this<ConnectionPool> {
 private:
  boost::asio::io_context& ioc_;
  std::string destIP_;
  uint16_t destPort_;
  std::shared_ptr<ConnectPolicy> policy_;
  std::array<std::weak_ptr<ConnectionInfo>, kMaxPoolSize> connections_;

  // Note, this is sorted by value.attemptAfter, to ensure that we queue
  // operations in the appropriate order
  boost::container::devector<PendingRequest> requestQueue_;

  // set to true when we're in process of pushing a message to the queue
  bool pushInProgress_ = false;
  std::shared_ptr<Channel> channel_;

  friend class Client;

  void QueuePending(PendingRequest&& pending);

  static void ChannelPushComplete(
      const std::weak_ptr<ConnectionPool>& weak_self,
      boost::system::error_code ec);

 public:
  ConnectionPool(boost::asio::io_context& ioc_in, std::string_view dest_ip_in,
                 uint16_t dest_port_in,
                 const std::shared_ptr<ConnectPolicy>& policy);

  ~ConnectionPool() {
    //fmt::print("destroying connection {}\n", fmt::ptr(this));
    for (auto& connection : connections_) {
      auto conn = connection.lock();
      if (conn) {
        conn->ShutdownConn();
      }
    }
  }

  ConnectionPool(const ConnectionPool&) = delete;
  ConnectionPool(ConnectionPool&&) = delete;
  ConnectionPool& operator=(const ConnectionPool&) = delete;
  ConnectionPool& operator=(ConnectionPool&&) = delete;
};

class Client {
 private:
  std::unordered_map<std::string, std::shared_ptr<ConnectionPool> >
      connectionPools_;

  std::shared_ptr<ConnectPolicy> policy_;
  boost::asio::io_context& ioc_;

 public:
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;
  Client(Client&&) = delete;
  Client& operator=(Client&&) = delete;
  ~Client() { connectionPools_.clear(); }

  Client(boost::asio::io_context& ioc_in, ConnectPolicy policy);

  // Send request to destIP:destPort and use the provided callback to
  // handle the response
  void SendData(std::string&& data, std::string_view dest_ip,
                uint16_t dest_port, std::string_view dest_uri,
                const boost::beast::http::fields& http_header,
                boost::beast::http::verb verb,
                const std::function<void(Response&&)>& res_handler);
};
}  // namespace http
