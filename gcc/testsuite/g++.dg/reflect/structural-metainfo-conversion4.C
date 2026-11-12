// -freflection-structural-metainfo-conversion: the library defaults the
// conversion function to std::meta::info instead of reflect_constant.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection -freflection-structural-metainfo-conversion" }

#include <meta>
#include <expected>
#include <inplace_vector>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <utility>
#include <type_traits>
#include <variant>

template <auto V> struct A {};

static_assert (std::is_same_v<A<std::optional<int> (3)>, A<std::optional<int> (3)>>);
static_assert (!std::is_same_v<A<std::optional<int> (3)>, A<std::optional<int> {}>>);
static_assert (!std::is_same_v<A<std::tuple<int, char> {1, 'a'}>,
			       A<std::tuple<int, char> {1, 'b'}>>);
static_assert (!std::is_same_v<A<std::variant<int, char> (1)>,
			       A<std::variant<int, char> ('a')>>);
static_assert (!std::is_same_v<A<std::expected<int, char> (1)>,
			       A<std::expected<int, char> {std::unexpect, 'a'}>>);
static_assert (std::is_same_v<A<std::inplace_vector<int, 2> {1, 2}>,
			      A<std::inplace_vector<int, 2> {1, 2}>>);

constexpr int arr[] = { 1, 2 };
static_assert (std::is_same_v<A<std::span<const int> {arr}>,
			      A<std::span<const int> {arr}>>);
constexpr char str[] = "abc";
static_assert (std::is_same_v<A<std::string_view {str, 3}>,
			      A<std::string_view {str, 3}>>);

// They're found as conversions, not as reflect_constant.
static_assert (static_cast<std::meta::info> (std::optional<int> (3))
	       == std::meta::reflect_constant (std::optional<int> (3)));

// std::constant_wrapper has a conversion to std::meta::info when it wraps
// one, but no non-static data members: it is structural anyway, and its
// conversion isn't consulted.
using CW = std::remove_cv_t<decltype (std::cw<^^int>)>;
static_assert (std::meta::info (std::cw<^^int>) == ^^int);
static_assert (std::is_same_v<A<std::cw<^^int>>, A<std::cw<^^int>>>);
static_assert (!std::is_same_v<A<std::cw<^^int>>, A<std::cw<^^float>>>);
template <CW V> struct B {};
B<std::cw<^^int>> b;
