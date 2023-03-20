#include <CLI/CLI.hpp>
#include <boost/asio/io_context.hpp>
#include <optional>

#include "http_client.hpp"

static void handle_response(const std::shared_ptr<http::Client>&,
                            http::Response&& res) {
  std::cout << "Got response " << res.body() << "\n";
}

int main(int argc, char** argv) {
  CLI::App app{"Redfish access tool"};

  http::ConnectPolicy policy;

  std::string host;
  app.add_option("--host", host, "Host to connect to");

  std::optional<int16_t> port = 443;
  app.add_option("--port", port, "Port to connect to");

  bool useTls = true;
  app.add_option("--tls", useTls, "Use TLS+HTTP");

  if (!port) {
    port = useTls ? 443 : 80;
  }

  app.add_option("--verify_server", policy.verify_server_certificate,
                 "Verify the servers TLS certificate");

  CLI::App* sensor = app.add_subcommand("sensor", "Sensor related subcommands");
  // Define options
  CLI::Option* sensor_list_opt = sensor->add_option("list", "Sensor list");

  CLI11_PARSE(app, argc, argv);

  if (*sensor_list_opt) {
    std::cout << "doing sensor list\n";
    return EXIT_SUCCESS;
  }

  boost::asio::io_context ioc;

  std::shared_ptr<http::Client> http =
      std::make_shared<http::Client>(ioc, std::move(policy));

  std::string id;
  boost::beast::http::fields headers;
  http->sendDataWithCallback(std::string(), host, *port, "/redfish/v1", useTls,
                             headers, boost::beast::http::verb::get,
                             std::bind_front(&handle_response, http));

  http.reset();
  ioc.run();

  return EXIT_SUCCESS;
}
