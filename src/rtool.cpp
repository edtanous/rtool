#include <CLI/CLI.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/json.hpp>
#include <boost/json/basic_parser_impl.hpp>
#include <optional>

#include "http_client.hpp"

// The null parser discards all the data
class redpath_parser {
  struct handler {
    std::string redpath;
    bool redpath_match = true;
    constexpr static std::size_t max_object_size = std::size_t(-1);
    constexpr static std::size_t max_array_size = std::size_t(-1);
    constexpr static std::size_t max_key_size = std::size_t(-1);
    constexpr static std::size_t max_string_size = std::size_t(-1);

    using error_code = boost::json::error_code;

    bool on_document_begin(error_code&) { return true; }
    bool on_document_end(error_code&) { return true; }
    bool on_object_begin(error_code&) {
      if (!redpath_match) {
        redpath_match = true;
        return true;
      }

      return true;
    }
    bool on_object_end(std::size_t, error_code&) { return true; }
    bool on_array_begin(error_code&) {
      if (!redpath_match) {
        redpath_match = true;
        return true;
      }

      return true;
    }
    bool on_array_end(std::size_t, error_code&) { return true; }
    bool on_key_part(std::string_view key, std::size_t key_size, error_code&) {
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
    bool on_key(std::string_view key, std::size_t key_size, error_code& ec) {
      return on_key_part(key, key_size, ec);
    }
    bool on_string_part(std::string_view str, std::size_t, error_code&) {
      if (redpath_match) {
        std::cout << str;
      }
      return true;
    }
    bool on_string(std::string_view str, std::size_t str_size, error_code& ec) {
      if (!redpath_match) {
        redpath_match = true;
        return true;
      }
      on_string_part(str, str_size, ec);

      std::cout << "\n";

      return true;
    }
    bool on_number_part(std::string_view, error_code&) { return true; }
    bool on_int64(std::int64_t, std::string_view, error_code&) {
      if (!redpath_match) {
        redpath_match = true;
        return true;
      }

      return true;
    }
    bool on_uint64(std::uint64_t, std::string_view, error_code&) {
      if (!redpath_match) {
        redpath_match = true;
        return true;
      }
      return true;
    }
    bool on_double(double, std::string_view, error_code&) {
      if (!redpath_match) {
        redpath_match = true;
        return true;
      }

      return true;
    }
    bool on_bool(bool, error_code&) {
      if (!redpath_match) {
        redpath_match = true;
        return true;
      }

      return true;
    }
    bool on_null(error_code&) {
      if (!redpath_match) {
        redpath_match = true;
        return true;
      }

      return true;
    }
    bool on_comment_part(std::string_view, error_code&) { return false; }
    bool on_comment(std::string_view, error_code&) { return false; }
  };

  boost::json::basic_parser<handler> p_;

 public:
  redpath_parser(std::string_view redpath)
      : p_(boost::json::parse_options(), std::string(redpath)) {}

  ~redpath_parser() {}

  std::size_t write(char const* data, std::size_t size,
                    boost::system::error_code& ec) {
    auto const n = p_.write_some(false, data, size, ec);
    if (!ec && n < size) ec = boost::json::error::extra_data;
    return n;
  }
};

void pretty_print(std::ostream& os, boost::json::value const& jv,
                  std::string* indent = nullptr) {
  std::string indent_;
  if (!indent) indent = &indent_;
  switch (jv.kind()) {
    case boost::json::kind::object: {
      os << "{\n";
      indent->append(4, ' ');
      auto const& obj = jv.get_object();
      if (!obj.empty()) {
        auto it = obj.begin();
        for (;;) {
          os << *indent << boost::json::serialize(it->key()) << " : ";
          pretty_print(os, it->value(), indent);
          if (++it == obj.end()) break;
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
        auto it = arr.begin();
        for (;;) {
          os << *indent;
          pretty_print(os, *it, indent);
          if (++it == arr.end()) break;
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
      if (jv.get_bool())
        os << "true";
      else
        os << "false";
      break;

    case boost::json::kind::null:
      os << "null";
      break;
  }

  if (indent->empty()) os << "\n";
}

static void handle_response(const std::string& redpath,
                            const std::shared_ptr<http::Client>&,
                            http::Response&& res) {
  std::string_view ct = res.getHeader(boost::beast::http::field::content_type);
  if (ct != "application/json" && ct != "application/json; charset=utf-8") {
    return;
  }
  boost::system::error_code ec;
  redpath_parser p(redpath);
  p.write(res.body().data(), res.body().size(), ec);

  if (ec) {
    return;
  }
}

static void getRedpath(std::string_view host, uint16_t port,
                       const std::shared_ptr<http::Client>& client,
                       std::string_view redpath) {
  boost::beast::http::fields headers;
  client->sendData(
      std::string(), host, port, "/redfish/v1", headers,
      boost::beast::http::verb::get,
      std::bind_front(&handle_response, std::string(redpath), client));
}

int main(int argc, char** argv) {
  CLI::App app{"Redfish access tool"};

  http::ConnectPolicy policy;

  std::string host;
  app.add_option("--host", host, "Host to connect to");

  std::optional<int16_t> port = 443;
  app.add_option("--port", port, "Port to connect to");

  app.add_option("--tls", policy.useTls, "Use TLS+HTTP");

  if (!port) {
    port = policy.useTls ? 443 : 80;
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
      std::make_shared<http::Client>(ioc, std::move(policy));

  if (raw) {
    for (const std::string& redpath : redpaths) {
      getRedpath(host, *port, http, redpath);
    }
  } else {
  }
  http.reset();
  ioc.run();

  return EXIT_SUCCESS;
}
