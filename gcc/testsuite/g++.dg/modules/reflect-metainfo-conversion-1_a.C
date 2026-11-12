// -freflection-structural-metainfo-conversion: template parameter objects
// keyed by a conversion's result across a module boundary.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fmodules -freflection -freflection-structural-metainfo-conversion -Wno-global-module" }
// { dg-module-cmi M }

module;
#include <meta>
#include <utility>
export module M;
export class Frac
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
export template <int N, int D> constexpr Frac frac_canon (N, D);
consteval Frac::operator std::meta::info () const
{
  int g = gcd (n, d);
  return std::meta::substitute (^^frac_canon,
				{ std::meta::reflect_constant (n / g),
				  std::meta::reflect_constant (d / g) });
}
export struct Outer { Frac f; int k; };
export template <auto V> struct A { static constexpr auto v = V; };
export using A12 = A<Frac (1, 2)>;
export using AO = A<Outer{Frac (1, 2), 7}>;
