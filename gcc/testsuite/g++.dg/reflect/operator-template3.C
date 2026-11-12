// P4340R1: a user-provided template-argument function gives the template
// parameter object; subobjects are normalized to copies of theirs.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection -freflection-operator-template" }

#include <meta>
#include <numeric>
#include <type_traits>
using namespace std::meta;

struct Mod { int v; consteval operator template() const; };
template<int V> constexpr Mod canonical = {V};
consteval Mod::operator template() const {
  return substitute(^^canonical, {reflect_constant((v % 7 + 7) % 7)});
}
template<Mod m> struct X { static constexpr const Mod *addr = &m; };
static_assert(std::is_same_v<X<Mod{3}>, X<Mod{10}>>);
static_assert(X<Mod{10}>::addr == &canonical<3>);         // the object itself

struct Year { int y; consteval operator template() const; };
struct Date { Year year; int month, day; };
template<int N> constexpr Date jan1 = {{N}, 1, 1};
consteval Year::operator template() const {
  auto r = substitute(^^jan1, {reflect_constant(y)});
  return reflect_object(extract<const Date&>(r).year);
}
template<Year y> constexpr const Year* addr = &y;
static_assert(addr<Year{2024}> == &jan1<2024>.year);      // a subobject

struct Frac { int num, den; consteval operator template() const; };
template<int N, int D> constexpr Frac interned = {N, D};
consteval Frac::operator template() const {
  int g = std::gcd(num, den);
  return substitute(^^interned, {reflect_constant(num / g), reflect_constant(den / g)});
}
template<auto D> struct A { };
constexpr info r5 = reflect_constant(Frac{2, 4});
static_assert(is_object(r5));
static_assert(r5 == reflect_object(interned<1, 2>));
static_assert(r5 == template_arguments_of(^^A<Frac{3, 6}>)[0]);
template<Frac F> struct Y { };
static_assert(std::is_same_v<Y<Frac{2, 4}>, Y<interned<3, 6>>>);

struct Stuff { Frac f; int k; };
template<Stuff S> struct Z { static constexpr int n = S.f.num; };
static_assert(std::is_same_v<Z<Stuff{{2, 4}, 1}>, Z<Stuff{{1, 2}, 1}>>);
static_assert(Z<Stuff{{2, 4}, 1}>::n == 1);              // normalized copy

template <auto V> int use () { return 0; }
int u = use<Mod{3}> () + use<Year{2024}> () + use<Stuff{{2, 4}, 1}> ();

struct Bad { int v; consteval operator template() const { return ^^canonical<0>; } };
template<Bad b> struct W { };
W<Bad{1}> w;	// { dg-error "must give a reflection of an object of type .const Bad." }
