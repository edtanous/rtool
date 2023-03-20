#pragma once
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <nlohmann/json.hpp>
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
  using response_type =
      boost::beast::http::response<boost::beast::http::string_body>;

  std::optional<response_type> stringResponse;

  std::string_view getHeader(boost::beast::http::field key) {
    return (*stringResponse)[key];
  }

  Response() : stringResponse(response_type{}) {}

  explicit Response(response_type&& stringResponseIn)
      : stringResponse(stringResponseIn) {}

  ~Response() = default;

  Response(Response&& res) = delete;
  Response(const Response&) = delete;

  Response& operator=(const Response& r) = delete;

  Response& operator=(Response&& r) = delete;

  boost::beast::http::status result() const { return stringResponse->result(); }

  std::string& body() { return stringResponse->body(); }

  std::string_view getHeaderValue(std::string_view key) const {
    return stringResponse->base()[key];
  }

  void clear() { stringResponse.emplace(response_type{}); }
};
}  // namespace http
