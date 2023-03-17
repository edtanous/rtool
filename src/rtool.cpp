#include <CLI/CLI.hpp>
#include <boost/asio/io_context.hpp>

#include "http_client.hpp"

static void handle_response(const std::shared_ptr<http::Client>&,
                            http::Response&& res) {
  std::cout << "Got response " << res.body() << "\n";
}

int main(int argc, char** argv) {
  CLI::App app{"Redfish access tool"};

  std::string host;
  app.add_option("--host", host, "Host to connect to");

  bool verify_server_tls = true;
  app.add_option("--verify_server", verify_server_tls,
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
  std::shared_ptr<http::Client> http = std::make_shared<http::Client>(ioc);

  std::string id;
  boost::beast::http::fields headers;
  http->sendDataWithCallback(
      std::string(), id, "192.168.7.2", 443, "/redfish/v1", true, headers,
      boost::beast::http::verb::get, std::bind_front(&handle_response, http));

  ioc.run();

  return EXIT_SUCCESS;
}
