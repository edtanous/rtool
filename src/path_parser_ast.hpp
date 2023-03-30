#pragma once
#include <boost/fusion/include/adapt_struct.hpp>
#include <boost/spirit/home/x3.hpp>
#include <boost/spirit/home/x3/support/ast/variant.hpp>
#include <iostream>
#include <list>
#include <numeric>
#include <string>

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
  std::vector<path_component> filters;
  auto operator<=>(const path&) const = default;
};

inline std::ostream& operator<<(std::ostream& os, const key_filter& filt) {
  os << filt.key;
  return os;
}

inline std::ostream& operator<<(std::ostream& os, const path& path) {
  os << "path[";
  for (const auto& filter : path.filters) {
    std::visit([&os](auto&& out) { os << out << ','; }, filter);
  }
  os << ']';
  return os;
}

}  // namespace filter_ast
}  // namespace redfish

BOOST_FUSION_ADAPT_STRUCT(redfish::filter_ast::key_filter, key, filter)
BOOST_FUSION_ADAPT_STRUCT(redfish::filter_ast::path, filters)
