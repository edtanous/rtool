#include "path_parser.hpp"

#include <optional>

#include "gmock/gmock.h"

using redfish::filter_ast::key_name;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::FieldsAre;
using ::testing::IsEmpty;
using ::testing::Optional;
using ::testing::VariantWith;

using redfish::filter_ast::key_filter;

TEST(FilterParser, BasicTypes) {
  // Basic number types
  EXPECT_THAT(parseRedfishPath("Chassis"),
              Optional(FieldsAre(VariantWith<key_name>(key_name("Chassis")),
                                 IsEmpty())));

  EXPECT_THAT(parseRedfishPath("Chassis[*]"),
              Optional(FieldsAre(VariantWith<key_filter>(key_filter{
                                     .key = "Chassis", .filter = '*'}),
                                 IsEmpty())));
  EXPECT_THAT(
      parseRedfishPath("Chassis[*]/Sensors"),
      Optional(FieldsAre(
          VariantWith<key_filter>(key_filter{.key = "Chassis", .filter = '*'}),
          ElementsAre(VariantWith<key_name>(key_name("Sensors"))))));
}

TEST(FilterParser, StringRoundTrip) {
  EXPECT_EQ(parseRedfishPath("Chassis[*]/Sensors")->to_path_string(),
            "Chassis[*]/Sensors");
}
TEST(FilterParser, ParentPath) {
  EXPECT_THAT(parseRedfishPath("Chassis[*]/Sensors")->strip_parent(),
              Optional(FieldsAre(VariantWith<key_name>(key_name("Sensors")),
                                 IsEmpty())));
}
