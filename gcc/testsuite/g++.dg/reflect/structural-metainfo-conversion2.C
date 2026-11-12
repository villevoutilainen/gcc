// -freflection-structural-metainfo-conversion: a user-provided conversion
// function to std::meta::info makes a literal class that isn't otherwise
// structural a structural type, whose values are
// template-argument-equivalent when the conversion gives the same
// reflection for them.  The template parameter object is a copy of the
// first such argument.
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
  consteval explicit operator std::meta::info () const
  {
    int g = gcd (n, d);
    return std::meta::reflect_constant (std::pair<int, int> (n / g, d / g));
  }
};

template <Frac F> struct A
{
  static constexpr int num = F.num ();
  static constexpr int den = F.den ();
};
static_assert (std::is_same_v<A<Frac (1, 2)>, A<Frac (2, 4)>>);
static_assert (!std::is_same_v<A<Frac (1, 2)>, A<Frac (1, 3)>>);
// A<Frac (1, 2)> and A<Frac (1, 3)> came first: their arguments are the
// ones that were copied.
static_assert (A<Frac (2, 4)>::num == 1);
static_assert (A<Frac (3, 9)>::num == 1);
static_assert (A<Frac (3, 9)>::den == 3);
static_assert (A<Frac (6, 8)>::num == 6);	// first for 3/4
static_assert (A<Frac (3, 4)>::num == 6);

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

// Only as a template parameter's own type, not as a subobject.
struct Outer { Frac f; };
template <Outer V> struct D {};	// { dg-error "not a valid type for a template non-type parameter" }

