// -freflection-structural-metainfo-conversion: a class that is
// structural only through a user-provided conversion to std::meta::info
// is also structural as a subobject, at any depth, and is compared there
// by its conversion's result.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection -freflection-structural-metainfo-conversion" }

#include <meta>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

class Frac
{
  int n, d;
  static consteval int gcd (int a, int b) { return b ? gcd (b, a % b) : a; }
public:
  consteval Frac (int x, int y) : n (x), d (y) {}
  consteval int num () const { return n; }
  consteval explicit operator std::meta::info () const;
};
// The canonical, lowest-terms value: the template parameter object of every
// equivalent argument.
template <int N, int D> constexpr Frac frac_canon (N, D);
consteval Frac::operator std::meta::info () const
{
  int g = gcd (n, d);
  return std::meta::substitute (^^frac_canon,
				{ std::meta::reflect_constant (n / g),
				  std::meta::reflect_constant (d / g) });
}

template <auto V> struct A {};

// A plain aggregate with a Frac member.
struct Outer { Frac f; int k; };
static_assert (std::is_same_v<A<Outer{Frac (1, 2), 7}>, A<Outer{Frac (2, 4), 7}>>);
static_assert (!std::is_same_v<A<Outer{Frac (1, 2), 7}>, A<Outer{Frac (1, 2), 8}>>);

// An array member.
struct Arr { Frac a[2]; };
static_assert (std::is_same_v<A<Arr{{Frac (1, 2), Frac (1, 3)}}>, A<Arr{{Frac (2, 4), Frac (2, 6)}}>>);
static_assert (!std::is_same_v<A<Arr{{Frac (1, 2), Frac (1, 3)}}>, A<Arr{{Frac (1, 3), Frac (1, 2)}}>>);

// Library wrappers, nested.
static_assert (std::is_same_v<A<std::optional<std::optional<Frac>> (Frac (1, 2))>,
			      A<std::optional<std::optional<Frac>> (Frac (3, 6))>>);
static_assert (!std::is_same_v<A<std::optional<std::optional<Frac>> (Frac (1, 2))>,
			       A<std::optional<std::optional<Frac>> (std::optional<Frac> {})>>);
static_assert (std::is_same_v<A<std::tuple<int, Frac> {1, Frac (1, 2)}>,
			      A<std::tuple<int, Frac> {1, Frac (4, 8)}>>);

// A union.
union U { int i; Frac f; consteval U (Frac x) : f (x) {} consteval U (int v) : i (v) {} };
static_assert (std::is_same_v<A<U (Frac (1, 2))>, A<U (Frac (2, 4))>>);
static_assert (!std::is_same_v<A<U (Frac (1, 2))>, A<U (1)>>);

// Two levels of user-provided conversions: the outer one's canonical
// variable is a specialization for its Frac members, which are equivalent
// by Frac's conversion.
class Interval
{
  Frac lo, hi;
public:
  consteval Interval (Frac a, Frac b) : lo (a), hi (b) {}
  consteval Frac low () const { return lo; }
  consteval explicit operator std::meta::info () const;
};
template <Frac L, Frac H> constexpr Interval interval_canon (L, H);
consteval Interval::operator std::meta::info () const
{
  return std::meta::substitute (^^interval_canon,
				{ std::meta::reflect_constant (lo),
				  std::meta::reflect_constant (hi) });
}
static_assert (std::is_same_v<A<Interval (Frac (1, 2), Frac (1, 1))>,
			      A<Interval (Frac (2, 4), Frac (3, 3))>>);
static_assert (!std::is_same_v<A<Interval (Frac (1, 2), Frac (1, 1))>,
			       A<Interval (Frac (1, 3), Frac (1, 1))>>);


// The template parameter object has the canonical value of each such
// subobject, whichever argument came first.
template <auto V> struct B { static constexpr auto v = V; };
static_assert (B<Outer{Frac (2, 4), 7}>::v.f.num () == 1);
static_assert (B<Outer{Frac (1, 2), 7}>::v.f.num () == 1);
static_assert (B<Interval (Frac (2, 4), Frac (3, 3))>::v.low ().num () == 1);
