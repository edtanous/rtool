#pragma once

#include <boost/json.hpp>
#include <string>

void PrettyPrint(std::ostream& os, boost::json::value const& jv,
                 std::string* indent = nullptr);
