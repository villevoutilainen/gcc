// -freflection-structural-metainfo-conversion: a user-provided conversion
// function to std::meta::info makes a literal class that isn't otherwise
// structural a structural type, whose values are
// template-argument-equivalent when the conversion gives the same
// reflection for them.  That reflection must be of a variable of type
// const T, whose value is the template parameter object.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection -freflection-structural-metainfo-conversion" }

#include <meta>
#include <type_traits>
#include <utility>

class Frac
{
  int n, d;
  static consteval int gcd (int a, int b) { return b ? gcd (b, a % b) : a; }
public:
  consteval Frac (int x, int y) : n (x), d (y) {}
  consteval int num () const { return n; }
  consteval int den () const { return d; }
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

template <Frac F> struct A
{
  static constexpr int num = F.num ();
  static constexpr int den = F.den ();
};
static_assert (std::is_same_v<A<Frac (1, 2)>, A<Frac (2, 4)>>);
static_assert (!std::is_same_v<A<Frac (1, 2)>, A<Frac (1, 3)>>);
// The template parameter object is the canonical value, whichever
// argument came first.
static_assert (A<Frac (2, 4)>::num == 1);
static_assert (A<Frac (3, 9)>::num == 1);
static_assert (A<Frac (3, 9)>::den == 3);
static_assert (A<Frac (6, 8)>::num == 3);
static_assert (A<Frac (6, 8)>::den == 4);

// The key includes the type: an auto parameter tells them apart.
template <auto V> struct B {};
static_assert (!std::is_same_v<B<Frac (1, 2)>, B<std::pair<int, int> (1, 2)>>);
static_assert (std::is_same_v<B<Frac (1, 2)>, B<Frac (5, 10)>>);

// std::meta::reflect_constant gives the template parameter object.
static_assert (std::meta::reflect_constant (Frac (1, 2))
	       == std::meta::reflect_constant (Frac (4, 8)));
static_assert ([: std::meta::reflect_constant (Frac (4, 8)) :].num () == 1);

// An otherwise structural class keeps member-wise equivalence: the
// conversion isn't consulted.
struct Pub
{
  int x;
  consteval explicit operator std::meta::info () const { return ^^int; }
};
template <Pub V> struct C {};
static_assert (!std::is_same_v<C<Pub{1}>, C<Pub{2}>>);

// Also as a subobject, compared by its conversion's result
// (structural-metainfo-conversion6.C).
struct Outer { Frac f; };
template <Outer V> struct D {};
static_assert (std::is_same_v<D<Outer{Frac (1, 2)}>, D<Outer{Frac (2, 4)}>>);


// The conversion must give a reflection of a variable of type const T.
class Bad
{
  int x;
public:
  consteval Bad (int v) : x (v) {}
  consteval explicit operator std::meta::info () const { return ^^int; }
};
template <Bad V> struct E {};
E<Bad (1)> e;	// { dg-error "must give a reflection of a variable of type .const Bad." }
