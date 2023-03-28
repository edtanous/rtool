#pragma once

#include "path_parser_ast.hpp"

#include <boost/spirit/home/x3.hpp>

namespace redfish::filter_grammar
{

namespace details
{
// clang-format off
using boost::spirit::x3::rule;
rule<class expression, filter_ast::key_filter> const expression("expression");
rule<class key_name, filter_ast::key_name> const key_name("key_name");
// clang-format on

using boost::spirit::x3::char_;
using boost::spirit::x3::lit;

auto const key_name_def = char_("A-Z") >> *(char_("a-zA-Z0-9"));

auto const expression_def = key_name >> -(lit('[') >> lit('*') >> lit(']'));

BOOST_SPIRIT_DEFINE(key_name, expression);

inline auto grammar = expression;
} // namespace details

using details::grammar;

} // namespace redfish::filter_grammar

std::optional<redfish::filter_ast::key_filter> parseFilterExpression(std::string_view expr);

