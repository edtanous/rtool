#pragma once

#include <boost/spirit/home/x3.hpp>

#include "path_parser_ast.hpp"

namespace redfish::filter_grammar {

namespace details {
// clang-format off
using boost::spirit::x3::rule;
rule<class expression, filter_ast::key_filter> const expression("expression");
rule<class key_name, filter_ast::key_name> const key_name("key_name");
rule<class path_component, filter_ast::path_component> const path_component("path_component");
rule<class path, filter_ast::path> const path("path");
// clang-format on

using boost::spirit::x3::char_;
using boost::spirit::x3::lit;

auto const key_name_def = char_("A-Z") >> *(char_("a-zA-Z0-9"));

auto const expression_def = key_name >> lit('[') >> char_('*') >> lit(']');

auto const path_component_def = (expression | key_name);

auto const path_def = +(path_component);

BOOST_SPIRIT_DEFINE(key_name, expression, path_component, path);

inline auto grammar = path;
}  // namespace details

using details::grammar;

}  // namespace redfish::filter_grammar

std::optional<redfish::filter_ast::path> parseFilterExpression(
    std::string_view expr);
