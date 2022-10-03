#include <boost/asio/io_context.hpp>
#include "http_client.hpp"

int main(int /*argc*/, char** /*argv[]*/)
{
    boost::asio::io_context ioc;

    http::Client http(ioc);

    return EXIT_SUCCESS;
}
