#pragma once
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
#include "http_response.hpp"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <queue>
#include <string>

namespace crow
{

    // It is assumed that the BMC should be able to handle 4 parallel
    // connections
    constexpr uint8_t maxPoolSize = 4;
    constexpr uint8_t maxRequestQueueSize = 50;
    constexpr unsigned int httpReadBodyLimit = 131072;
    constexpr unsigned int httpReadBufferSize = 4096;

    struct PendingRequest
    {
        boost::beast::http::request<boost::beast::http::string_body> req;
        std::function<void(Response&&)> callback;
        PendingRequest(
            boost::beast::http::request<boost::beast::http::string_body>&&
                reqIn,
            const std::function<void(Response&&)>& callbackIn)
            : req(std::move(reqIn)), callback(callbackIn)
        {
        }
        PendingRequest() = default;
    };

    using Channel = boost::asio::experimental::channel<void(
        boost::system::error_code, PendingRequest)>;

    class ConnectionInfo : public std::enable_shared_from_this<ConnectionInfo>
    {
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

        void doResolve()
        {
            resolver.async_resolve(
                host, std::to_string(port),
                std::bind_front(
                    &ConnectionInfo::afterResolve, this, shared_from_this()));
        }

        void afterResolve(
            const std::shared_ptr<ConnectionInfo>& /*self*/,
            const boost::system::error_code ec,
            const boost::asio::ip::tcp::resolver::results_type& endpointList)
        {
            if (ec || (endpointList.empty())) { return; }

            timer.expires_after(std::chrono::seconds(30));
            timer.async_wait(std::bind_front(onTimeout, weak_from_this()));

            boost::asio::async_connect(
                conn, endpointList,
                std::bind_front(
                    &ConnectionInfo::afterConnect, this, shared_from_this()));
        }

        void afterConnect(
            const std::shared_ptr<ConnectionInfo>& /*self*/,
            boost::beast::error_code ec,
            const boost::asio::ip::tcp::endpoint& /*endpoint*/)
        {
            timer.cancel();
            if (ec) { return; }
            if (sslConn) {
                doSslHandshake();
                return;
            }
            sendMessage();
        }

        void doSslHandshake()
        {
            if (!sslConn) { return; }
            timer.expires_after(std::chrono::seconds(30));
            timer.async_wait(std::bind_front(onTimeout, weak_from_this()));
            sslConn->async_handshake(
                boost::asio::ssl::stream_base::client,
                std::bind_front(
                    &ConnectionInfo::afterSslHandshake, this,
                    shared_from_this()));
        }

        void afterSslHandshake(
            const std::shared_ptr<ConnectionInfo>& /*self*/,
            boost::beast::error_code ec)
        {
            timer.cancel();
            if (ec) { return; }
            sendMessage();
        }

        void sendMessage()
        {
            channel->async_receive(std::bind_front(
                &ConnectionInfo::onMessageReadyToSend, this,
                shared_from_this()));

            conn.async_wait(
                boost::asio::ip::tcp::socket::wait_error,
                std::bind_front(
                    &ConnectionInfo::onIdleEvent, this, weak_from_this()));
        }

        void onMessageReadyToSend(
            const std::shared_ptr<ConnectionInfo>& /*self*/,
            boost::system::error_code ec,
            PendingRequest pending)
        {
            if (ec) { return; }

            // Cancel our idle waiting event
            conn.cancel(ec);
            // intentionally ignore errors here.  It's possible there was
            // nothing in progress to cancel

            req = std::move(pending.req);
            callback = std::move(pending.callback);

            // Set a timeout on the operation
            timer.expires_after(std::chrono::seconds(30));
            timer.async_wait(std::bind_front(onTimeout, weak_from_this()));

            // Send the HTTP request to the remote host
            if (sslConn) {
                boost::beast::http::async_write(
                    *sslConn, *req,
                    std::bind_front(
                        &ConnectionInfo::afterWrite, this, shared_from_this()));
            }
            else {
                boost::beast::http::async_write(
                    conn, *req,
                    std::bind_front(
                        &ConnectionInfo::afterWrite, this, shared_from_this()));
            }
        }

        void onIdleEvent(
            const std::weak_ptr<ConnectionInfo>& /*self*/,
            const boost::system::error_code& ec)
        {
            if (ec && ec != boost::asio::error::operation_aborted) {
                doClose();
            }
        }

        void afterWrite(
            const std::shared_ptr<ConnectionInfo>& /*self*/,
            const boost::beast::error_code& ec,
            size_t /*bytesTransferred*/)
        {
            timer.cancel();
            if (ec) {
                callback(Response(parser->release()));
                return;
            }

            recvMessage();
        }

        void recvMessage()
        {
            parser.emplace(std::piecewise_construct, std::make_tuple());
            parser->body_limit(httpReadBodyLimit);

            timer.expires_after(std::chrono::seconds(30));
            timer.async_wait(std::bind_front(onTimeout, weak_from_this()));

            // Receive the HTTP response
            if (sslConn) {
                boost::beast::http::async_read(
                    *sslConn, buffer, *parser,
                    std::bind_front(
                        &ConnectionInfo::afterRead, this, shared_from_this()));
            }
            else {
                boost::beast::http::async_read(
                    conn, buffer, *parser,
                    std::bind_front(
                        &ConnectionInfo::afterRead, this, shared_from_this()));
            }
        }

        void afterRead(
            const std::shared_ptr<ConnectionInfo>& /*self*/,
            const boost::beast::error_code& ec,
            const std::size_t /*bytesTransferred*/)
        {
            timer.cancel();
            if (ec && ec != boost::asio::ssl::error::stream_truncated) {
                callback(Response(parser->release()));
                return;
            }
            // Keep the connection alive if server supports it
            // Else close the connection

            // Copy the response into a Response object so that it can be
            // processed by the callback function.
            callback(Response(parser->release()));

            // Callback has served its purpose, let it destruct
            callback = nullptr;

            // Is more data is now loaded for the next request?
            if (parser->keep_alive()) { sendMessage(); }
            else {
                // Server is not keep-alive enabled so we need to close the
                // connection and then start over from resolve
                doClose();
                doResolve();
            }
        }

        static void onTimeout(
            const std::weak_ptr<ConnectionInfo>& weakSelf,
            const boost::system::error_code ec)
        {
            if (ec == boost::asio::error::operation_aborted) { return; }
            if (ec) {
                // If the timer fails, we need to close the socket anyway, same
                // as if it expired.
            }
            std::shared_ptr<ConnectionInfo> self = weakSelf.lock();
            if (self == nullptr) { return; }
            self->doClose();
        }

        void onTimerDone(
            const std::shared_ptr<ConnectionInfo>& /*self*/,
            const boost::system::error_code& ec)
        {
            if (ec == boost::asio::error::operation_aborted) { return; }
            else if (ec) {
            }

            // Let's close the connection
            doClose();
        }

        void shutdownConn()
        {
            boost::beast::error_code ec;
            conn.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
            conn.close();

            // not_connected happens sometimes so don't bother reporting it.
            if (ec && ec != boost::beast::errc::not_connected) {}
            else {
            }
        }

        void doClose()
        {
            if (!sslConn) {
                shutdownConn();
                return;
            }

            sslConn->async_shutdown(std::bind_front(
                &ConnectionInfo::afterSslShutdown, this, shared_from_this()));
        }

        void afterSslShutdown(
            const std::shared_ptr<ConnectionInfo>& /*self*/,
            const boost::system::error_code& ec)
        {
            if (ec) {}
            else {
            }
            shutdownConn();
        }

        void setCipherSuiteTLSext()
        {
            if (!sslConn) { return; }
            // NOTE: The SSL_set_tlsext_host_name is defined in tlsv1.h header
            // file but its having old style casting (name is cast to void*).
            // Since bmcweb compiler treats all old-style-cast as error, its
            // causing the build failure. So replaced the same macro inline and
            // did corrected the code by doing static_cast to viod*. This has to
            // be fixed in openssl library in long run. Set SNI Hostname (many
            // hosts need this to handshake successfully)
            if (SSL_ctrl(
                    sslConn->native_handle(), SSL_CTRL_SET_TLSEXT_HOSTNAME,
                    TLSEXT_NAMETYPE_host_name,
                    static_cast<void*>(&host.front())) == 0)

            {
                boost::beast::error_code ec{
                    static_cast<int>(::ERR_get_error()),
                    boost::asio::error::get_ssl_category()};

                return;
            }
        }

      public:
        explicit ConnectionInfo(
            boost::asio::io_context& iocIn,
            const std::string& destIPIn,
            uint16_t destPortIn,
            bool useSSL,
            unsigned int connIdIn,
            const std::shared_ptr<Channel>& channelIn)
            : host(destIPIn)
            , port(destPortIn)
            , connId(connIdIn)
            , resolver(iocIn)
            , conn(iocIn)
            , timer(iocIn)
            , channel(channelIn)
        {
            if (useSSL) {
                boost::asio::ssl::context sslCtx(
                    boost::asio::ssl::context::tls_client);

                boost::system::error_code ec;

                // Support only TLS v1.2 & v1.3
                sslCtx.set_options(
                    boost::asio::ssl::context::default_workarounds |
                        boost::asio::ssl::context::no_sslv2 |
                        boost::asio::ssl::context::no_sslv3 |
                        boost::asio::ssl::context::single_dh_use |
                        boost::asio::ssl::context::no_tlsv1 |
                        boost::asio::ssl::context::no_tlsv1_1,
                    ec);
                if (ec) { return; }

                // Add a directory containing certificate authority files to be
                // used for performing verification.
                sslCtx.set_default_verify_paths(ec);
                if (ec) { return; }

                // Verify the remote server's certificate
                sslCtx.set_verify_mode(boost::asio::ssl::verify_peer, ec);
                if (ec) { return; }

                // All cipher suites are set as per OWASP datasheet.
                // https://cheatsheetseries.owasp.org/cheatsheets/TLS_Cipher_String_Cheat_Sheet.html
                constexpr const char* sslCiphers =
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

                if (SSL_CTX_set_cipher_list(
                        sslCtx.native_handle(), sslCiphers) != 1) {
                    return;
                }

                sslConn.emplace(conn, sslCtx);
                setCipherSuiteTLSext();
            }
            doResolve();
        }
    };

    class ConnectionPool : public std::enable_shared_from_this<ConnectionPool>
    {
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

        friend class HttpClient;

        void queuePending(PendingRequest&& pending)
        {
            // If we have to queue it, push it into the request queue in time
            // order
            if (pushInProgress) {
                if (requestQueue.size() >= maxRequestQueueSize) { return; }
                requestQueue.emplace_back(std::move(pending));
                return;
            }

            // Make sure we have some connections open ready to receive
            for (std::weak_ptr<ConnectionInfo>& weakConn : connections) {
                std::shared_ptr<ConnectionInfo> conn = weakConn.lock();
                if (conn == nullptr) { continue; }

                static unsigned int newId = 0;
                newId++;
                conn = std::make_shared<ConnectionInfo>(
                    ioc, destIP, destPort, useSSL, newId, channel);
                weakConn = conn->weak_from_this();

                // Only need to construct one extra connection max
                break;
            }
            pushInProgress = true;

            channel->async_send(
                boost::system::error_code(), std::move(pending),
                std::bind_front(&ConnectionPool::channelPushComplete, this));
        }

        void channelPushComplete(boost::system::error_code ec)
        {
            pushInProgress = false;
            if (ec) { return; }
            if (!requestQueue.empty()) {
                pushInProgress = true;

                channel->async_send(
                    boost::system::error_code(),
                    std::move(requestQueue.front()),
                    std::bind_front(
                        &ConnectionPool::channelPushComplete, this));
                requestQueue.pop_front();
            }
        }

      public:
        explicit ConnectionPool(
            boost::asio::io_context& iocIn,
            const std::string& idIn,
            const std::string& destIPIn,
            uint16_t destPortIn,
            bool useSSLIn)
            : ioc(iocIn)
            , id(idIn)
            , destIP(destIPIn)
            , destPort(destPortIn)
            , useSSL(useSSLIn)
        {
        }
    };

    class HttpClient
    {
      private:
        std::unordered_map<std::string, std::shared_ptr<ConnectionPool> >
            connectionPools;
        boost::asio::io_context& ioc;
        HttpClient() = default;

        // Used as a dummy callback by sendData() in order to call
        // sendDataWithCallback()
        static void genericResHandler(const Response& /*res*/) {}

      public:
        HttpClient(const HttpClient&) = delete;
        HttpClient& operator=(const HttpClient&) = delete;
        HttpClient(HttpClient&&) = delete;
        HttpClient& operator=(HttpClient&&) = delete;
        ~HttpClient() = default;

        HttpClient(boost::asio::io_context& iocIn) : ioc(iocIn) {}

        // Send a request to destIP:destPort where additional processing of the
        // result is not required
        void sendData(
            std::string&& data,
            const std::string& id,
            const std::string& destIP,
            uint16_t destPort,
            const std::string& destUri,
            bool useSSL,
            const boost::beast::http::fields& httpHeader,
            const boost::beast::http::verb verb)
        {
            const std::function<void(Response &&)> cb = genericResHandler;
            sendDataWithCallback(
                std::move(data), id, destIP, destPort, destUri, useSSL,
                httpHeader, verb, cb);
        }

        // Send request to destIP:destPort and use the provided callback to
        // handle the response
        void sendDataWithCallback(
            std::string&& data,
            const std::string& id,
            const std::string& destIP,
            uint16_t destPort,
            const std::string& destUri,
            bool useSSL,
            const boost::beast::http::fields& httpHeader,
            const boost::beast::http::verb verb,
            const std::function<void(Response&&)>& resHandler)
        {
            std::string clientKey = useSSL ? "https" : "http";
            clientKey += destIP;
            clientKey += ":";
            clientKey += std::to_string(destPort);
            // Use nullptr to avoid creating a ConnectionPool each time
            std::shared_ptr<ConnectionPool>& conn = connectionPools[clientKey];
            if (conn == nullptr) {
                // Now actually create the ConnectionPool shared_ptr since it
                // does not already exist
                conn = std::make_shared<ConnectionPool>(
                    ioc, id, destIP, destPort, useSSL);
            }
            else {
            }

            // Send the data using either the existing connection pool or the
            // newly created connection pool Construct the request to be sent
            boost::beast::http::request<boost::beast::http::string_body>
                thisReq(verb, destUri, 11, "", httpHeader);
            thisReq.set(boost::beast::http::field::host, destIP);
            thisReq.keep_alive(true);
            thisReq.body() = std::move(data);
            thisReq.prepare_payload();
            conn->queuePending(PendingRequest(std::move(thisReq), resHandler));
        }
    };
} // namespace crow
