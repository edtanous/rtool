#pragma once
#include <optional>
#include <string_view>

#include "path_parser_ast.hpp"

std::optional<redfish::filter_ast::path> parseRedfishPath(
    std::string_view expr);
