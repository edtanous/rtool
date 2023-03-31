#pragma once
#include <boost/fusion/include/adapt_struct.hpp>
#include <iostream>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace redfish {
namespace filter_ast {

// Represents a string that matches an identifier
struct key_name : std::string {
  auto operator<=>(const key_name&) const = default;
};

struct key_filter {
  std::string key;
  char filter;
  auto operator<=>(const key_filter&) const = default;
};

using path_component = std::variant<key_name, key_filter>;

struct path {
  path_component first;
  std::vector<path_component> filters;
  auto operator<=>(const path&) const = default;

  void append_path(std::string& path_str, const path_component& path);

  std::string to_path_string();

  std::optional<path> strip_parent() const;
};

}  // namespace filter_ast
}  // namespace redfish
BOOST_FUSION_ADAPT_STRUCT(redfish::filter_ast::key_filter, key, filter)
BOOST_FUSION_ADAPT_STRUCT(redfish::filter_ast::path, first, filters)
