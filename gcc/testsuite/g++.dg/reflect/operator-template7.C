// P4340R1: string literals as template arguments.  A pointer into, or a
// reference to, a string literal object is normalized to the corresponding
// object of the template parameter object std::meta::reflect_constant_string
// gives for it.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection -freflection-operator-template" }

#include <meta>
#include <numeric>
#include <string_view>
#include <type_traits>
using namespace std::meta;
// Our std::define_static_string returns a string_view; this is the pointer.
#define DSS(s) (&[: reflect_constant_string (s) :][0])
template<const char* p> struct X { };
static_assert (std::is_same_v<X<"hello">, X<DSS ("hello")>>);
constexpr const char* a = "holdup";
static_assert (!std::is_same_v<X<a + 3>, X<"dup">>);
static_assert (std::is_same_v<X<a + 3>, X<"holdup" + 3>>);
template<const char& r> struct W { };
static_assert (std::is_same_v<W<"hello"[1]>, W<[: reflect_constant_string ("hello") :][1]>>);

struct Frac { int num, den; consteval operator template() const; };
template<int N, int D> constexpr Frac interned = {N, D};
consteval Frac::operator template() const {
  int g = std::gcd(num, den);
  return substitute(^^interned, {reflect_constant(num / g), reflect_constant(den / g)});
}
struct Stuff { Frac f; const char* s; };
template<Stuff S> struct Z { };
static_assert (std::is_same_v<Z<Stuff{{2, 4}, "hi"}>, Z<Stuff{{1, 2}, DSS ("hi")}>>);

struct K { char const* p; };
constexpr info r4 = reflect_constant(K{"ebab"});
static_assert (extract<K>(r4).p == DSS ("ebab"));

// A string_view (defaulted operator template) holding a literal.
template<std::string_view S> struct V { static constexpr auto s = S; };
static_assert (std::is_same_v<V<"hello">, V<std::string_view (DSS ("hello"), 5)>>);
static_assert (V<"hello">::s == "hello");

// Array member initialized from a literal stays a value.
struct Arr { char c[4]; };
template<Arr A> struct Q { };
static_assert (std::is_same_v<Q<Arr{"abc"}>, Q<Arr{{'a', 'b', 'c', 0}}>>);

