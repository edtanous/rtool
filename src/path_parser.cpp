#include "path_parser.hpp"

#include <iostream>
#include <list>
#include <numeric>
#include <string>

#include "path_parser_ast.hpp"

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

std::optional<redfish::filter_ast::path> parseFilterExpression(
    std::string_view expr) {
  auto& calc = redfish::filter_grammar::grammar;
  redfish::filter_ast::path program;

  std::string_view::iterator iter = expr.begin();
  const std::string_view::iterator end = expr.end();
  bool r = boost::spirit::x3::parse(iter, end, calc, program);

  if (!r) {
    std::cout << "Parsing failed\n";
    std::string rest(iter, end);
    std::cout << "stopped at: \"" << rest << "\"\n";
  }
  return program;
}
