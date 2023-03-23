#pragma once

#include <fmt/core.h>

#include <boost/beast/core.hpp>
#include <boost/system/error_code.hpp>

template <>
struct fmt::formatter<boost::system::error_code> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) {
    return ctx.begin();
  }
  template <typename FormatContext>
  auto format(const boost::system::error_code& ec, FormatContext& ctx) {
    return fmt::format_to(ctx.out(), "{}: {}", ec.category().name(),
                          ec.value());
  }
};
