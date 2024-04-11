#pragma once

#include <format>

#include <boost/beast/core.hpp>
#include <boost/system/error_code.hpp>

template <>
struct std::formatter<boost::system::error_code> {
  constexpr auto parse(auto& ctx) {
    return ctx.begin();
  }
  auto format(const boost::system::error_code& ec, auto& ctx) const noexcept {
    return std::format_to(ctx.out(), "{} {}: {}", ec.category().name(), ec.message(),
                          ec.value());
  }
};
