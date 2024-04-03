#pragma once
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <optional>
#include <string>
#include <string_view>

#include "hex_utils.hpp"

namespace http {

template <typename Adaptor, typename Handler>
class Connection;

struct Response {
  template <typename Adaptor, typename Handler>
  friend class Connection;
  using ResponseType =
      boost::beast::http::response<boost::beast::http::string_body>;

  std::optional<ResponseType> string_response;

  std::string_view GetHeader(boost::beast::http::field key) {
    return (*string_response)[key];
  }

  Response() : string_response(ResponseType{}) {}

  explicit Response(ResponseType&& string_response_in)
      : string_response(string_response_in) {}

  ~Response() = default;

  Response(Response&& res) = delete;
  Response(const Response&) = delete;

  Response& operator=(const Response& r) = delete;

  Response& operator=(Response&& r) = delete;

  boost::beast::http::status Result() const {
    return string_response->result();
  }

  std::string& Body() { return string_response->body(); }

  std::string_view GetHeaderValue(std::string_view key) const {
    return string_response->base()[key];
  }

  void Clear() { string_response.emplace(ResponseType{}); }
};
}  // namespace http
