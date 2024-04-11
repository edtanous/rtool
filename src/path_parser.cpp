#include "path_parser.hpp"

#include <iostream>
#include <list>
#include <numeric>
#include <string>

#include "path_parser_ast.hpp"
#include "path_parser_internal.hpp"

#include <spdlog/spdlog.h>

namespace redfish {
namespace ast {
///////////////////////////////////////////////////////////////////////////
//  The AST Printer
///////////////////////////////////////////////////////////////////////////
struct printer {
  typedef void result_type;

  void operator()(
      filter_ast::key_filter const& /*x*/) const {  // std::cout << x.key;
  }
};

///////////////////////////////////////////////////////////////////////////
//  The AST evaluator
///////////////////////////////////////////////////////////////////////////
struct eval {
  typedef int result_type;

  void operator()(
      filter_ast::key_filter const& /*x*/) const {  // std::cout << x.key;
  }
};
}  // namespace ast
}  // namespace redfish

std::optional<redfish::filter_ast::path> parseRedfishPath(
    std::string_view expr) {
  auto& calc = redfish::filter_grammar::grammar;
  redfish::filter_ast::path program;

  std::string_view::iterator iter = expr.begin();
  bool r = boost::spirit::x3::parse(iter, expr.end(), calc, program);

  if (!r || iter != expr.end()) {
    SPDLOG_ERROR("Parsing failed");
    std::string rest(iter, expr.end());
    SPDLOG_ERROR("stopped at: \"{}\"", rest);
    return std::nullopt;
  }
  return program;
}
