#include <CLI/CLI.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/json.hpp>
#include <boost/json/basic_parser_impl.hpp>
#include <optional>

#include "http_client.hpp"

// The null parser discards all the data
class RedpathParser {
  // NOLINTBEGIN
  struct Handler {
    explicit Handler(std::string_view redpath) : redpath(redpath) {}
    std::string redpath;
    bool redpath_match = true;
    constexpr static std::size_t max_object_size = std::size_t(-1);
    constexpr static std::size_t max_array_size = std::size_t(-1);
    constexpr static std::size_t max_key_size = std::size_t(-1);
    constexpr static std::size_t max_string_size = std::size_t(-1);

    static bool on_document_begin(std::error_code& /*unused*/) { return true; }
    static bool on_document_end(std::error_code& /*unused*/) { return true; }
    bool on_object_begin(std::error_code& /*unused*/) {
      if (!redpath_match) {
        redpath_match = true;
        return true;
      }

      return true;
    }
    static bool on_object_end(std::size_t /*unused*/,
                              std::error_code& /*unused*/) {
      return true;
    }
    bool on_array_begin(std::error_code& /*unused*/) {
      if (!redpath_match) {
        redpath_match = true;
        return true;
      }

      return true;
    }
    static bool on_array_end(std::size_t /*unused*/,
                             std::error_code& /*unused*/) {
      return true;
    }
    bool on_key_part(std::string_view key, std::size_t key_size,
                     std::error_code& /*unused*/) {
      std::string_view sv(redpath);
      std::cout << "Comparing " << key << " to "
                << sv.substr(key_size - key.size(), key.size()) << "\n";
      std::cout << "redpath_match before is " << redpath_match << "\n";
      bool sub_matches = sv.substr(key_size - key.size(), key.size()) == key;
      std::cout << "Sub matches " << sub_matches << "\n";
      redpath_match = redpath_match && sub_matches;
      std::cout << "redpath_match is " << redpath_match << "\n";
      return true;
    }
    bool on_key(std::string_view key, std::size_t key_size,
                std::error_code& ec) {
      return on_key_part(key, key_size, ec);
    }
    bool on_string_part(std::string_view str, std::size_t /*unused*/,
                        std::error_code& /*unused*/) const {
      if (redpath_match) {
        std::cout << str;
      }
      return true;
    }
    bool on_string(std::string_view str, std::size_t str_size,
                   std::error_code& ec) {
      if (!redpath_match) {
        redpath_match = true;
        return true;
      }
      on_string_part(str, str_size, ec);

      std::cout << "\n";

      return true;
    }
    static bool on_number_part(std::string_view /*unused*/,
                               std::error_code& /*unused*/) {
      return true;
    }
    bool on_int64(std::int64_t /*unused*/, std::string_view /*unused*/,
                  std::error_code& /*unused*/) {
      if (!redpath_match) {
        redpath_match = true;
        return true;
      }

      return true;
    }
    bool on_uint64(std::uint64_t /*unused*/, std::string_view /*unused*/,
                   std::error_code& /*unused*/) {
      if (!redpath_match) {
        redpath_match = true;
        return true;
      }
      return true;
    }
    bool on_double(double /*unused*/, std::string_view /*unused*/,
                   std::error_code& /*unused*/) {
      if (!redpath_match) {
        redpath_match = true;
        return true;
      }

      return true;
    }
    bool on_bool(bool /*unused*/, std::error_code& /*unused*/) {
      if (!redpath_match) {
        redpath_match = true;
        return true;
      }

      return true;
    }
    bool on_null(std::error_code& /*unused*/) {
      if (!redpath_match) {
        redpath_match = true;
        return true;
      }

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
  explicit RedpathParser(std::string_view redpath)
      : p_(boost::json::parse_options(), redpath) {}

  std::size_t Write(char const* data, std::size_t size,
                    boost::system::error_code& ec) {
    auto const n = p_.write_some(false, data, size, ec);
    if (!ec && n < size) {
      ec = boost::json::error::extra_data;
    }
    return n;
  }
};

void PrettyPrint(std::ostream& os, boost::json::value const& jv,
                 std::string* indent = nullptr) {
  std::string local_indent;
  if (indent == nullptr) {
    indent = &local_indent;
  }
  switch (jv.kind()) {
    case boost::json::kind::object: {
      os << "{\n";
      indent->append(4, ' ');
      auto const& obj = jv.get_object();
      if (!obj.empty()) {
        boost::json::object::const_iterator it = obj.begin();
        for (;;) {
          os << *indent << boost::json::serialize(it->key()) << " : ";
          PrettyPrint(os, it->value(), indent);
          if (++it == obj.end()) {
            break;
          }
          os << ",\n";
        }
      }
      os << "\n";
      indent->resize(indent->size() - 4);
      os << *indent << "}";
      break;
    }

    case boost::json::kind::array: {
      os << "[\n";
      indent->append(4, ' ');
      auto const& arr = jv.get_array();
      if (!arr.empty()) {
        boost::json::array::const_iterator it = arr.begin();
        for (;;) {
          os << *indent;
          PrettyPrint(os, *it, indent);
          if (++it == arr.end()) {
            break;
          }
          os << ",\n";
        }
      }
      os << "\n";
      indent->resize(indent->size() - 4);
      os << *indent << "]";
      break;
    }

    case boost::json::kind::string: {
      os << boost::json::serialize(jv.get_string());
      break;
    }

    case boost::json::kind::uint64:
      os << jv.get_uint64();
      break;

    case boost::json::kind::int64:
      os << jv.get_int64();
      break;

    case boost::json::kind::double_:
      os << jv.get_double();
      break;

    case boost::json::kind::bool_:
      if (jv.get_bool()) {
        os << "true";
      } else {
        os << "false";
      }
      break;

    case boost::json::kind::null:
      os << "null";
      break;
  }

  if (indent->empty()) {
    os << "\n";
  }
}

static void HandleResponse(const std::string& redpath,
                           const std::shared_ptr<http::Client>& /*unused*/,
                           http::Response&& res) {
  std::string_view ct = res.GetHeader(boost::beast::http::field::content_type);
  if (ct != "application/json" && ct != "application/json; charset=utf-8") {
    return;
  }
  boost::system::error_code ec;
  RedpathParser p(redpath);
  p.Write(res.Body().data(), res.Body().size(), ec);

  if (ec) {
    return;
  }
}

static void GetRedpath(std::string_view host, uint16_t port,
                       const std::shared_ptr<http::Client>& client,
                       std::string_view redpath) {
  boost::beast::http::fields headers;
  client->SendData(
      std::string(), host, port, "/redfish/v1", headers,
      boost::beast::http::verb::get,
      std::bind_front(&HandleResponse, std::string(redpath), client));
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
    std::cout << "doing sensor list\n";
    return EXIT_SUCCESS;
  }

  boost::asio::io_context ioc;

  std::shared_ptr<http::Client> http =
      std::make_shared<http::Client>(ioc, policy);

  if (raw != nullptr) {
    for (const std::string& redpath : redpaths) {
      GetRedpath(host, *port, http, redpath);
    }
  } else {
  }
  http.reset();
  ioc.run();

  return EXIT_SUCCESS;
}
