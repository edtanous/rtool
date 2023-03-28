#include "path_parser.hpp"

#include "gmock/gmock.h"

using ::testing::ElementsAre;
using ::testing::FieldsAre;
using ::testing::Optional;

TEST(FilterParser, BasicTypes) {
  // Basic number types
  EXPECT_THAT(parseFilterExpression("Chassis"),
              Optional(FieldsAre(ElementsAre(FieldsAre("Chassis")))));

  EXPECT_TRUE(parseFilterExpression("Chassis[*]"));
}
