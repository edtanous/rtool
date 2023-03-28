#pragma once
#include <boost/fusion/include/adapt_struct.hpp>
#include <boost/spirit/home/x3.hpp>
#include <boost/spirit/home/x3/support/ast/variant.hpp>

#include <iostream>
#include <list>
#include <numeric>
#include <string>

namespace redfish
{
namespace filter_ast
{

// Represents a string that matches an identifier
struct key_name: std::string
{};

// An expression that has been negated with not()
struct key_filter
{
    key_name key;
    auto operator<=>(const key_filter&) const = default;
};

} // namespace filter_ast
} // namespace redfish

BOOST_FUSION_ADAPT_STRUCT(redfish::filter_ast::key_filter, key)
