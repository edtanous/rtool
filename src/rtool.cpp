#include <CLI/CLI.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/json.hpp>
#include <boost/json/basic_parser_impl.hpp>
#include <limits>
#include <optional>

#include "boost_formatter.hpp"
#include "http_client.hpp"
#include "json.hpp"
#include "path_parser.hpp"
#include "path_parser_fmt_printers.hpp"

// The null parser discards all the data
class RedpathParser {
  // Handler methods don't follow the naming convention.
  // NOLINTBEGIN
  struct MatchedProperty {
    redfish::filter_ast::path key_path;
    std::string value;
  };
  struct Handler {
    explicit Handler(std::vector<redfish::filter_ast::path>&& redpaths_in) {
      for (const auto& redpath : redpaths_in) {
        redpaths.emplace_back(std::move(redpath), "");
      }
    }
    std::vector<MatchedProperty> release() { return std::move(redpaths); }

    // Each redpath in order, as well as how many key strings have been matched
    std::vector<MatchedProperty> redpaths;
    std::string current_key;
    std::string current_value;
    constexpr static std::size_t max_object_size = std::size_t(-1);
    constexpr static std::size_t max_array_size = std::size_t(-1);
    constexpr static std::size_t max_key_size = std::size_t(-1);
    constexpr static std::size_t max_string_size = std::size_t(-1);

    static bool on_document_begin(std::error_code& /*unused*/) { return true; }
    static bool on_document_end(std::error_code& /*unused*/) { return true; }
    bool on_object_begin(std::error_code& /*unused*/) {
      current_key += "/";
      return true;
    }
    bool on_object_end(std::size_t /*unused*/, std::error_code& /*unused*/) {
      current_key = current_key.substr(0, current_key.find('/') - 1);
      return true;
    }
    bool on_array_begin(std::error_code& /*unused*/) { return true; }
    bool on_array_end(std::size_t /*unused*/, std::error_code& /*unused*/) {
      return true;
    }
    bool on_key_part(std::string_view key, std::size_t /*key_size*/,
                     std::error_code& /*unused*/) {
      current_key += key;
      return true;
    }
    bool on_key(std::string_view key, std::size_t key_size,
                std::error_code& ec) {
      if (!on_key_part(key, key_size, ec)) {
        return false;
      }
      return true;
    }
    bool on_string_part(std::string_view str, std::size_t /*unused*/,
                        std::error_code& /*unused*/) {
      current_value += str;
      return true;
    }
    bool on_string(std::string_view str, std::size_t str_size,
                   std::error_code& ec) {
      if (!on_string_part(str, str_size, ec)) {
        return false;
      }

      for (auto& redpath : redpaths) {
        // fmt::print("checking {} == {}\n", redpath.first, current_key);
        if (const redfish::filter_ast::key_name* match =
                std::get_if<redfish::filter_ast::key_name>(
                    &redpath.key_path.first)) {
          if (*match == current_key) {
            redpath.value = std::move(current_value);
            break;
          }
        }
      }
      current_key = "";
      current_value = "";

      return true;
    }
    bool on_bool(bool /*unused*/, std::error_code& /*unused*/) { return true; }
    bool on_number_part(std::string_view /*unused*/,
                        std::error_code& /*unused*/) {
      return true;
    }
    bool on_int64(std::int64_t /*unused*/, std::string_view /*unused*/,
                  std::error_code& /*unused*/) {
      return true;
    }
    bool on_uint64(std::uint64_t /*unused*/, std::string_view /*unused*/,
                   std::error_code& /*unused*/) {
      return true;
    }
    bool on_double(double /*unused*/, std::string_view /*unused*/,
                   std::error_code& /*unused*/) {
      return true;
    }
    bool on_null(std::error_code& /*unused*/) { return true; }
    static bool on_comment_part(std::string_view /*unused*/,
                                std::error_code& /*unused*/) {
      return false;
    }
    static bool on_comment(std::string_view /*unused*/,
                           std::error_code& /*unused*/) {
      return false;
    }
    // NOLINTEND
  };

  boost::json::basic_parser<Handler> p_;

 public:
  explicit RedpathParser(std::vector<redfish::filter_ast::path>&& redpaths)
      : p_(boost::json::parse_options(), std::move(redpaths)) {}

  std::vector<MatchedProperty> release() { return p_.handler().release(); }

  std::size_t Write(char const* data, std::size_t size,
                    boost::system::error_code& ec) {
    auto const n = p_.write_some(false, data, size, ec);
    if (!ec && n < size) {
      ec = boost::json::error::extra_data;
    }
    return n;
  }
};

static void HandleResponse(std::vector<redfish::filter_ast::path> redpaths,
                           const std::shared_ptr<http::Client>& /*unused*/,
                           http::Response&& res) {
  std::string_view ct = res.GetHeader(boost::beast::http::field::content_type);
  if (ct != "application/json" && ct != "application/json; charset=utf-8") {
    return;
  }
  boost::system::error_code ec;
  RedpathParser p(std::move(redpaths));
  p.Write(res.Body().data(), res.Body().size(), ec);
  auto redpaths_out = p.release();
  for (auto& redpath : redpaths_out) {
    if (const redfish::filter_ast::key_filter* filter =
            std::get_if<redfish::filter_ast::key_filter>(
                &redpath.key_path.first)) {
      if (filter->filter == '*') {
        fmt::print("Resolving {}",
                   redpath.key_path.strip_parent().to_path_string());
      }
    } else if (!redpath.value.empty()) {
      fmt::print("{}={}\n", redpath.key_path.to_path_string(), redpath.value);
    }
  }
  if (ec) {
    return;
  }
}

static void GetRedpath(std::string_view host, uint16_t port,
                       const std::shared_ptr<http::Client>& client,
                       std::vector<redfish::filter_ast::path>&& redpaths) {
  boost::beast::http::fields headers;
  client->SendData(
      std::string(), host, port, "/redfish/v1", headers,
      boost::beast::http::verb::get,
      std::bind_front(&HandleResponse, std::move(redpaths), client));
}

int main(int argc, char** argv) {
  CLI::App app{"Redfish access tool"};

  http::ConnectPolicy policy;

  std::string host;
  app.add_option("--host", host, "Host to connect to");

  std::optional<int16_t> port = 443;
  app.add_option("--port", port, "Port to connect to");

  app.add_option("--tls", policy.use_tls, "Use TLS+HTTP");

  if (!port) {
    port = policy.use_tls ? 443 : 80;
  }

  app.add_option("--verify_server,!--no-verify-server",
                 policy.verify_server_certificate,
                 "Verify the servers TLS certificate");

  CLI::App* sensor = app.add_subcommand("sensor", "Sensor related subcommands");
  // Define options
  CLI::Option* sensor_list_opt = sensor->add_option("list", "Sensor list");

  CLI::App* raw = app.add_subcommand("raw", "Raw property gets");
  std::vector<std::string> redpaths;
  raw->add_option("get", redpaths, "Gets a list of properties");

  CLI11_PARSE(app, argc, argv);

  if (*sensor_list_opt) {
    return EXIT_SUCCESS;
  }

  boost::asio::io_context ioc;

  std::shared_ptr<http::Client> http =
      std::make_shared<http::Client>(ioc, policy);

  if (raw != nullptr) {
    std::vector<redfish::filter_ast::path> paths;
    for (const auto& redpath : redpaths) {
      std::optional<redfish::filter_ast::path> path = parseRedfishPath(redpath);
      if (!path) {
        fmt::print("Path {} was not valid\n", redpath);
        return EXIT_FAILURE;
      }
      paths.emplace_back(std::move(*path));
    }
    GetRedpath(host, *port, http, std::move(paths));
  } else {
  }
  http.reset();
  ioc.run();

  return EXIT_SUCCESS;
}
