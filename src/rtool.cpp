#include <signal.h>  // ::signal, ::raise
#include <spdlog/spdlog.h>

#include <CLI/CLI.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/json.hpp>
#include <boost/json/basic_parser_impl.hpp>
#include <boost/stacktrace.hpp>
#include <limits>
#include <optional>

#include "boost_formatter.hpp"
#include "http_client.hpp"
#include "json.hpp"
#include "path_parser.hpp"
#include "path_parser_fmt_printers.hpp"

class RedpathParser {
  // Handler methods don't follow the naming convention.
  // NOLINTBEGIN
  struct MatchedProperty {
    redfish::filter_ast::path key_path;
    std::string value;
  };
  struct Handler {
    explicit Handler(std::vector<redfish::filter_ast::path>&& redpaths_in) {
      for (const redfish::filter_ast::path& redpath : redpaths_in) {
        redpaths.emplace_back(MatchedProperty{std::move(redpath), ""});
      }
    }
    std::vector<MatchedProperty> release() { return std::move(redpaths); }

    // Each redpath in order, as well as how many key strings have been matched
    std::vector<MatchedProperty> redpaths;
    std::string current_key = "/";
    std::string current_value;
    constexpr static std::size_t max_object_size = std::size_t(-1);
    constexpr static std::size_t max_array_size = std::size_t(-1);
    constexpr static std::size_t max_key_size = std::size_t(-1);
    constexpr static std::size_t max_string_size = std::size_t(-1);

    static bool on_document_begin(std::error_code& /*unused*/) { return true; }
    static bool on_document_end(std::error_code& /*unused*/) { return true; }
    bool on_object_begin(std::error_code& /*unused*/) {
      // SPDLOG_DEBUG("Object begin in object {}", current_key);
      // current_key += "/";
      return true;
    }

    void pop_value() {
      current_key = current_key.substr(0, current_key.rfind('/'));
      current_key = current_key.substr(0, current_key.rfind('/') + 1);
    }

    bool on_object_end(std::size_t /*unused*/, std::error_code& /*unused*/) {
      // SPDLOG_DEBUG("Object end in object {}", current_key);
      pop_value();
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
      current_key += "/";
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
      SPDLOG_DEBUG("value for {} was {}",
                   current_key.substr(0, current_key.size() - 1), str);

      for (auto& redpath : redpaths) {
        if (const redfish::filter_ast::key_name* match =
                std::get_if<redfish::filter_ast::key_name>(
                    &redpath.key_path.first)) {
          std::string odata = match->str() + "/@odata.id";
          SPDLOG_DEBUG("checking {} == {}", odata, current_key);

          if (odata == current_key) {
            // Found match
            SPDLOG_DEBUG("Found match {}", current_key);
            redpath.value = std::move(current_value);
            break;
          }
        }
        if (const redfish::filter_ast::key_filter* path =
                std::get_if<redfish::filter_ast::key_filter>(
                    &redpath.key_path.first)) {
          if (path->key + "/@odata.id" == current_key) {
            redpath.value = std::move(current_value);
          }
        }
      }

      pop_value();

      // current_key = "";
      current_value = "";

      return true;
    }
    bool on_bool(bool value, std::error_code& /*unused*/) {
      SPDLOG_DEBUG("Value of {} was {}",
                   current_key.substr(0, current_key.size() - 1), value);
      pop_value();

      return true;
    }
    bool on_number_part(std::string_view /*unused*/,
                        std::error_code& /*unused*/) {
      return true;
    }
    bool on_int64(std::int64_t value, std::string_view /*unused*/,
                  std::error_code& /*unused*/) {
      SPDLOG_DEBUG("Value of {} was {}",
                   current_key.substr(0, current_key.size() - 1), value);
      pop_value();

      return true;
    }
    bool on_uint64(std::uint64_t value, std::string_view /*unused*/,
                   std::error_code& /*unused*/) {
      SPDLOG_DEBUG("Value of {} was {}",
                   current_key.substr(0, current_key.size() - 1), value);
      pop_value();
      return true;
    }
    bool on_double(double value, std::string_view /*unused*/,
                   std::error_code& /*unused*/) {
      SPDLOG_DEBUG("Value of {} was {}",
                   current_key.substr(0, current_key.size() - 1), value);
      pop_value();
      return true;
    }
    bool on_null(std::error_code& /*unused*/) {
      SPDLOG_DEBUG("Value of {} was {} null",
                   current_key.substr(0, current_key.size() - 1));
      pop_value();

      return true;
    }
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

struct HostConnectData {
  std::string host;
  uint16_t port = 443;
  std::string username;
  std::string password;
};

static void GetRedpath(const std::string& base_uri, HostConnectData host,
                       const std::shared_ptr<http::Client>& client,
                       std::vector<redfish::filter_ast::path>&& redpaths);

static void HandleResponse(std::vector<redfish::filter_ast::path> redpaths,
                           const std::shared_ptr<http::Client>& client,
                           HostConnectData host, http::Response&& res) {
  SPDLOG_DEBUG("Got response {}", res.Body());
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
      if (filter->key == "") {
      } else if (filter->filter == '*') {
        std::optional<redfish::filter_ast::path> parent =
            redpath.key_path.strip_parent();
        if (parent) {
          SPDLOG_DEBUG("Resolving {}", parent->to_path_string());
          std::vector<redfish::filter_ast::path> new_paths;
          new_paths.push_back(*parent);
          GetRedpath(redpath.value, host, client, std::move(new_paths));
        } else {
          SPDLOG_DEBUG("Couldn't resolve parent of {}",
                       redpath.key_path.to_path_string());
        }
      }
    } else if (!redpath.value.empty()) {
      SPDLOG_DEBUG("{}={}", redpath.key_path.to_path_string(), redpath.value);
    }
  }
  if (ec) {
    return;
  }
}

static void GetRedpath(const std::string& base_uri, HostConnectData host,
                       const std::shared_ptr<http::Client>& client,
                       std::vector<redfish::filter_ast::path>&& redpaths) {
  boost::beast::http::fields headers;
  std::string auth = host.username + ":" + host.password;
  std::string auth_out;
  /*
  auth_out.resize(boost::beast::detail::base64::encoded_size(auth.size()));
  auth_out.resize(boost::beast::detail::base64::encode(
      auth_out.data(), auth.data(), auth.size()));
  SPDLOG_DEBUG("Got base64 {}", auth_out);
  headers.set(boost::beast::http::field::authorization, "Basic " + auth_out);
  */
  client->SendData(
      std::string(), host.host, host.port, base_uri, headers,
      boost::beast::http::verb::get,
      std::bind_front(&HandleResponse, std::move(redpaths), client, host));
}

struct RawGetOptions {
  std::vector<std::string> redpaths;
};

void run_raw_get_cmd(const RawGetOptions& opts,
                     const http::ConnectPolicy& policy,
                     const HostConnectData& host) {
  boost::asio::io_context ioc;

  std::shared_ptr<http::Client> http =
      std::make_shared<http::Client>(ioc, policy);

  std::vector<redfish::filter_ast::path> paths;
  for (const auto& redpath : opts.redpaths) {
    std::optional<redfish::filter_ast::path> path = parseRedfishPath(redpath);
    if (!path) {
      SPDLOG_DEBUG("Path {} was not valid", redpath);
      return;
    }
    paths.emplace_back(std::move(*path));
  }
  for (auto& path : paths) {
    SPDLOG_DEBUG("{}", path);
  }

  GetRedpath("/redfish/v1", host, http, std::move(paths));
  ioc.run();
}

void my_signal_handler(int signum) {
  ::signal(signum, SIG_DFL);
  boost::stacktrace::safe_dump_to("./backtrace.dump");
  ::raise(SIGABRT);
}

void my_terminate_handler() {
  try {
    SPDLOG_CRITICAL(boost::stacktrace::to_string(boost::stacktrace::stacktrace()));
  } catch (...) {
  }
  std::abort();
}

int main(int argc, char** argv) {
  ::signal(SIGSEGV, &my_signal_handler);
  ::signal(SIGABRT, &my_signal_handler);

  std::set_terminate(&my_terminate_handler);

  constexpr std::string_view backtraceFilename = "./backtrace.dump";
  boost::system::error_code ec;
  if (std::filesystem::exists(backtraceFilename, ec)) {
    // there is a backtrace
    std::ifstream ifs{std::string(backtraceFilename)};

    boost::stacktrace::stacktrace st =
        boost::stacktrace::stacktrace::from_dump(ifs);
    SPDLOG_CRITICAL("Previous run crashed:\n{}",
                    boost::stacktrace::to_string(st));

    // cleaning up
    ifs.close();
    std::filesystem::remove(backtraceFilename);
  }

  spdlog::set_level(spdlog::level::debug);
  SPDLOG_DEBUG("Rtool started");

  SPDLOG_DEBUG("Parsing CLI");

  CLI::App app{"Redfish access tool"};

  std::shared_ptr<http::ConnectPolicy> policy =
      std::make_shared<http::ConnectPolicy>();

  std::shared_ptr<HostConnectData> host = std::make_shared<HostConnectData>();
  app.add_option("--host", host->host, "Host to connect to");

  app.add_option("--user", host->username, "Username to use");

  app.add_option("--pass", host->password, "Password to use");

  std::optional<uint16_t> port;
  app.add_option("--port", port, "Port to connect to");

  app.add_flag("--tls", policy->use_tls, "Use TLS+HTTP");

  app.add_flag("--verify_server,!--no-verify-server",
               policy->verify_server_certificate,
               "Verify the servers TLS certificate");

  CLI::App* sensor = app.add_subcommand("sensor", "Sensor related subcommands");
  // Define options
  CLI::Option* sensor_list_opt = sensor->add_option("list", "Sensor list");

  auto raw_opt = std::make_shared<RawGetOptions>();
  CLI::App* raw = app.add_subcommand("raw", "Raw property gets");

  CLI::App* raw_get = app.add_subcommand("get", "Get values");

  raw_get->add_option("redpaths", raw_opt->redpaths,
                      "Gets a list of properties");

  raw->callback(
      [raw_opt, policy, host]() { run_raw_get_cmd(*raw_opt, *policy, *host); });

  // Make sure we get at least one subcommand
  app.require_subcommand();

  CLI11_PARSE(app, argc, argv);
  SPDLOG_DEBUG("CLI Parsed");

  if (!port) {
    host->port = policy->use_tls ? 443 : 80;
  } else {
    host->port = *port;
  }

  if (*sensor_list_opt) {
    SPDLOG_DEBUG("Failed CLI");

    return EXIT_SUCCESS;
  }

  return EXIT_SUCCESS;
}
