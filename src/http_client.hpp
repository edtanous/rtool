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

#include "http_response.hpp"

namespace http {

// It is assumed that the BMC should be able to handle 4 parallel
// connections
constexpr uint8_t maxPoolSize = 4;
constexpr uint8_t maxRequestQueueSize = 50;
constexpr unsigned int httpReadBodyLimit = 131072;
constexpr unsigned int httpReadBufferSize = 4096;

struct PendingRequest {
  boost::beast::http::request<boost::beast::http::string_body> req;
  std::function<void(Response&&)> callback;
  PendingRequest(
      boost::beast::http::request<boost::beast::http::string_body>&& reqIn,
      const std::function<void(Response&&)>& callbackIn)
      : req(std::move(reqIn)), callback(callbackIn) {}
  PendingRequest() = default;
};

using Channel = boost::asio::experimental::concurrent_channel<void(
    boost::system::error_code, PendingRequest)>;

struct ConnectPolicy {
  bool verify_server_certificate = true;
};

class ConnectionInfo : public std::enable_shared_from_this<ConnectionInfo> {
 private:
  std::string host;
  uint16_t port;
  uint32_t connId;

  // Data buffers
  using BodyType = boost::beast::http::string_body;
  using RequestType = boost::beast::http::request<BodyType>;
  std::optional<RequestType> req;
  std::optional<boost::beast::http::response_parser<BodyType> > parser;
  boost::beast::flat_static_buffer<httpReadBufferSize> buffer;

  // Async callables
  std::function<void(Response&&)> callback;
  boost::asio::ip::tcp::resolver resolver;
  boost::asio::ip::tcp::socket conn;
  std::optional<boost::beast::ssl_stream<boost::asio::ip::tcp::socket&> >
      sslConn;

  boost::asio::steady_timer timer;

  std::shared_ptr<Channel> channel;

  friend class ConnectionPool;

  void doResolve();

  void afterResolve(
      const std::shared_ptr<ConnectionInfo>& /*self*/,
      const boost::system::error_code ec,
      const boost::asio::ip::tcp::resolver::results_type& endpointList);

  void afterConnect(const std::shared_ptr<ConnectionInfo>& /*self*/,
                    boost::beast::error_code ec,
                    const boost::asio::ip::tcp::endpoint& /*endpoint*/);

  void doSslHandshake();

  void afterSslHandshake(const std::shared_ptr<ConnectionInfo>& /*self*/,
                         boost::beast::error_code ec);

  void sendMessage();

  void onMessageReadyToSend(const std::shared_ptr<ConnectionInfo>& /*self*/,
                            boost::system::error_code ec,
                            PendingRequest pending);

  void onIdleEvent(const std::weak_ptr<ConnectionInfo>& /*self*/,
                   const boost::system::error_code& ec);

  void afterWrite(const std::shared_ptr<ConnectionInfo>& /*self*/,
                  const boost::beast::error_code& ec,
                  size_t /*bytesTransferred*/);

  void recvMessage();

  void afterRead(const std::shared_ptr<ConnectionInfo>& /*self*/,
                 const boost::beast::error_code& ec,
                 const std::size_t /*bytesTransferred*/);

  static void onTimeout(const std::weak_ptr<ConnectionInfo>& weakSelf,
                        const boost::system::error_code ec);

  void onTimerDone(const std::shared_ptr<ConnectionInfo>& /*self*/,
                   const boost::system::error_code& ec);

  void shutdownConn();

  void doClose();

  void afterSslShutdown(const std::shared_ptr<ConnectionInfo>& /*self*/,
                        const boost::system::error_code& ec);
  void setCipherSuiteTLSext();

 public:
  explicit ConnectionInfo(boost::asio::io_context& iocIn,
                          const std::string& destIPIn, uint16_t destPortIn,
                          bool useSSL, unsigned int connIdIn,
                          const ConnectPolicy& policy,
                          const std::shared_ptr<Channel>& channelIn);
  void start();
};

class ConnectionPool : public std::enable_shared_from_this<ConnectionPool> {
 private:
  boost::asio::io_context& ioc;
  std::string id;
  std::string destIP;
  uint16_t destPort;
  bool useSSL;
  std::array<std::weak_ptr<ConnectionInfo>, maxPoolSize> connections;

  // Note, this is sorted by value.attemptAfter, to ensure that we queue
  // operations in the appropriate order
  boost::container::devector<PendingRequest> requestQueue;

  // set to true when we're in process of pushing a message to the queue
  bool pushInProgress = false;
  std::shared_ptr<Channel> channel;

  friend class Client;

  void queuePending(PendingRequest&& pending);

  static void channelPushComplete(
      const std::weak_ptr<ConnectionPool>& weak_self,
      boost::system::error_code ec);

 public:
  explicit ConnectionPool(boost::asio::io_context& iocIn,
                          const std::string& idIn, const std::string& destIPIn,
                          uint16_t destPortIn, bool useSSLIn);

  ~ConnectionPool() {
    for (auto& connection : connections) {
      auto conn = connection.lock();
      if (conn) {
        conn->shutdownConn();
      }
    }
  }
};

class Client {
 private:
  std::unordered_map<std::string, std::shared_ptr<ConnectionPool> >
      connectionPools;
  boost::asio::io_context& ioc;
  Client() = default;

  // Used as a dummy callback by sendData() in order to call
  // sendDataWithCallback()
  static void genericResHandler(const Response& /*res*/);

 public:
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;
  Client(Client&&) = delete;
  Client& operator=(Client&&) = delete;
  ~Client() { connectionPools.clear(); }

  Client(boost::asio::io_context& iocIn);

  // Send request to destIP:destPort and use the provided callback to
  // handle the response
  void sendDataWithCallback(std::string&& data, const std::string& id,
                            const std::string& destIP, uint16_t destPort,
                            const std::string& destUri, bool useSSL,
                            const boost::beast::http::fields& httpHeader,
                            const boost::beast::http::verb verb,
                            const std::function<void(Response&&)>& resHandler);
};
}  // namespace http
