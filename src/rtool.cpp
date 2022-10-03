#include <CLI/CLI.hpp>
#include <boost/asio/io_context.hpp>

#include "http_client.hpp"

static void handle_response(http::Response&& /*res*/) {
  std::cout << "Got response\n";
}

int main(int argc, char** argv) {
  CLI::App app{"Redfish access tool"};

  CLI::App* sensor = app.add_subcommand("sensor", "Sensor related subcommands");
  // Define options
  CLI::Option* sensor_list_opt = sensor->add_option("list", "Sensor list");

  CLI11_PARSE(app, argc, argv);

  if (sensor_list_opt != nullptr) {
    std::cout << "doing sensor list\n";
    return EXIT_SUCCESS;
  }

  boost::asio::io_context ioc;
  http::Client http(ioc);

  std::string id;
  boost::beast::http::fields headers;
  http.sendDataWithCallback(std::string(), id, "192.168.7.2", 443,
                            "/redfish/v1", true, headers,
                            boost::beast::http::verb::get, &handle_response);

  ioc.run();

  return EXIT_SUCCESS;
}
