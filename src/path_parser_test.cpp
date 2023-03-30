#include "path_parser.hpp"

#include <optional>

#include "gmock/gmock.h"

using ::testing::ElementsAre;
using ::testing::FieldsAre;
using ::testing::IsEmpty;
using ::testing::Optional;
using ::testing::VariantWith;

TEST(FilterParser, BasicTypes) {
  // Basic number types
  using redfish::filter_ast::key_name;
  EXPECT_THAT(parseFilterExpression("Chassis"),
              Optional(FieldsAre(VariantWith<key_name>(key_name("Chassis")),
                                 IsEmpty())));

  using redfish::filter_ast::key_filter;
  EXPECT_THAT(parseFilterExpression("Chassis[*]"),
              Optional(FieldsAre(VariantWith<key_filter>(key_filter{
                                     .key = "Chassis", .filter = '*'}),
                                 IsEmpty())));
  EXPECT_THAT(
      parseFilterExpression("Chassis[*]/Sensors"),
      Optional(FieldsAre(
          VariantWith<key_filter>(key_filter{.key = "Chassis", .filter = '*'}),
          ElementsAre(VariantWith<key_name>(key_name("Sensors"))))));
}
