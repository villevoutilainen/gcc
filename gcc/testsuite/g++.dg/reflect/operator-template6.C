// P4340R1 with -freflection-operator-template: the library types the paper
// makes structural, by a defaulted operator template.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection -freflection-operator-template" }

#include <meta>
#include <bitset>
#include <chrono>
#include <compare>
#include <complex>
#include <functional>
#include <type_traits>
#include <expected>
#include <inplace_vector>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <variant>
using namespace std::chrono;
template <auto V> struct A {};

static int g;
static_assert (std::is_same_v<A<std::ref (g)>, A<std::ref (g)>>);
static_assert (std::is_same_v<A<seconds (5)>, A<seconds (5)>>);
static_assert (!std::is_same_v<A<seconds (5)>, A<seconds (6)>>);
static_assert (!std::is_same_v<A<sys_seconds (seconds (1))>, A<sys_seconds (seconds (2))>>);
static_assert (std::is_same_v<A<2024y / November / 12d>, A<2024y / November / 12d>>);
static_assert (!std::is_same_v<A<2024y / November / 12d>, A<2024y / November / 13d>>);
static_assert (!std::is_same_v<A<Monday[2]>, A<Monday[3]>>);
static_assert (!std::is_same_v<A<November / last>, A<December / last>>);
static_assert (!std::is_same_v<A<std::strong_ordering::less>, A<std::strong_ordering::greater>>);
static_assert (!std::is_same_v<A<std::partial_ordering::unordered>, A<std::partial_ordering::less>>);
static_assert (std::is_same_v<A<std::weak_ordering::equivalent>, A<std::weak_ordering::equivalent>>);
static_assert (!std::is_same_v<A<std::bitset<70> (5)>, A<std::bitset<70> (6)>>);
static_assert (!std::is_same_v<A<std::complex<double> (1, 2)>, A<std::complex<double> (1, 3)>>);
static_assert (!std::is_same_v<A<std::complex<int> (1, 2)>, A<std::complex<int> (1, 3)>>);

// The types libstdc++ already made structural in every mode.
static_assert (std::is_same_v<A<std::optional<int> (3)>, A<std::optional<int> (3)>>);
static_assert (!std::is_same_v<A<std::tuple<int, char> {1, 'a'}>, A<std::tuple<int, char> {1, 'b'}>>);
static_assert (!std::is_same_v<A<std::variant<int, char> (1)>, A<std::variant<int, char> ('a')>>);
static_assert (!std::is_same_v<A<std::expected<int, char> (1)>, A<std::expected<int, char> {std::unexpect, 'a'}>>);
static_assert (std::is_same_v<A<std::inplace_vector<int, 2> {1, 2}>, A<std::inplace_vector<int, 2> {1, 2}>>);
constexpr int arr[] = { 1, 2 };
static_assert (std::is_same_v<A<std::span<const int> {arr}>, A<std::span<const int> {arr}>>);
constexpr char str[] = "abc";
static_assert (std::is_same_v<A<std::string_view {str, 3}>, A<std::string_view {str, 3}>>);
