#pragma once
#include <spdlog/spdlog.h>

#include <boost/fusion/include/adapt_struct.hpp>
#include <boost/fusion/include/at_c.hpp>
#include <boost/fusion/include/for_each.hpp>
#include <boost/fusion/include/mpl.hpp>
#include <boost/fusion/include/zip.hpp>
#include <boost/mpl/range_c.hpp>
#include <boost/type_index.hpp>

#include "path_parser_ast.hpp"

template <typename FormatContext, typename Sequence>
struct Struct_member_printer {
  Struct_member_printer(const Sequence& seq, FormatContext& ctx)
      : seq_(seq), ctx_(ctx) {}
  const Sequence& seq_;
  FormatContext& ctx_;
  template <typename Index>
  void operator()(Index) const {
    std::string member_type =
        boost::typeindex::type_id<typename boost::fusion::result_of::value_at<
            Sequence, Index>::type>()
            .pretty_name();
    std::string member_name =
        boost::fusion::extension::struct_member_name<Sequence,
                                                     Index::value>::call();
    std::format_to(ctx_.out(), "    {} {}\n", member_type, member_name);
  }
};
template <typename FormatContext, typename Sequence>
auto print_struct(FormatContext& ctx, Sequence const& v) {
  typedef boost::mpl::range_c<unsigned, 0,
                              boost::fusion::result_of::size<Sequence>::value>
      Indices;
  std::format_to(ctx.out(), "\n{{\n");
  boost::fusion::for_each(
      Indices(), Struct_member_printer<FormatContext, Sequence>(v, ctx));
  return std::format_to(ctx.out(), "}}");
}

template <>
struct std::formatter<redfish::filter_ast::path> {
  template <typename ParseContext>
  constexpr auto parse(ParseContext& ctx) {
    return ctx.begin();
  }

  auto format(const redfish::filter_ast::path& p, auto& ctx) const noexcept{
    return print_struct(ctx, p);
  }
};
