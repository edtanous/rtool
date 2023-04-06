#include "path_parser_ast.hpp"

namespace redfish::filter_ast {
void path::append_path(std::string& path_str, const path_component& path) {
  struct VisitPath {
    std::string& str_;
    VisitPath(std::string& str) : str_(str) {}
    void operator()(const key_filter& p) {
      str_ += p.key;
      str_ += '[';
      str_ += p.filter;
      str_ += ']';
    }
    void operator()(const key_name& p) { str_ += p; }
  };
  std::visit(VisitPath(path_str), path);
}

std::string path::to_path_string() {
  std::string ret;
  append_path(ret, first);
  for (const path_component& p : filters) {
    ret += '/';
    append_path(ret, p);
  }
  return ret;
}

std::optional<path> path::strip_parent() const {
  if (filters.empty()) {
    return std::nullopt;
  }

  if (const key_filter* p = std::get_if<key_filter>(&first)) {
    if (p->key != "Members" && p->filter == '*') {
      key_filter filter{.key = "Members", .filter = p->filter};

      path foo{.first = filter, .filters = filters};
      return foo;
    }
  }

  return path{
      .first = filters[0],
      .filters = {filters.begin() + 1, filters.end()},
  };
}
}  // namespace redfish::filter_ast
