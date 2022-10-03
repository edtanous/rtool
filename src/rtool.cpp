#include <CLI/CLI.hpp>
#include <boost/asio/io_context.hpp>
#include "http_client.hpp"

int main(int argc, char** argv)
{
    CLI::App app{"Redfish access tool"};

    CLI::App* sensor =
        app.add_subcommand("sensor", "Sensor related subcommands");
    // Define options
    CLI::Option* sensor_list_opt = sensor->add_option("list", "Sensor list");

    CLI11_PARSE(app, argc, argv);

    if(sensor_list_opt != nullptr){
        std::cout << "doing sensor list\n";
        return EXIT_SUCCESS;
    }


    boost::asio::io_context ioc;
    http::Client http(ioc);

    ioc.run();

    return EXIT_SUCCESS;
}
