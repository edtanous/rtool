#pragma once
#include <iostream>
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
};

inline std::ostream& operator<<(std::ostream& os, const key_filter& filt) {
  os << filt.key;
  return os;
}

inline std::ostream& operator<<(std::ostream& os, const path& path) {
  os << "path[";
  std::visit([&os](auto&& out) { os << out << ','; }, path.first);
  for (const auto& filter : path.filters) {
    std::visit([&os](auto&& out) { os << out << ','; }, filter);
  }
  os << ']';
  return os;
}

}  // namespace filter_ast
}  // namespace redfish
