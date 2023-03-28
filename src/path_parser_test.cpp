#include "path_parser.hpp"

#include "gmock/gmock.h"

using ::testing::Optional;

TEST(FilterParser, BasicTypes)
{
    // Basic number types
 redfish::filter_ast::key_filter key({"Chassis"}) ;
  EXPECT_THAT(parseFilterExpression("Chassis"), Optional(key));
 
    EXPECT_TRUE(parseFilterExpression("Chassis[*]"));
}

