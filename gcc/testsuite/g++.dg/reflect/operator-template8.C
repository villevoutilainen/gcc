// P4340R1 [class.template.arg]/7.4: the complete object of the template
// parameter object a user-provided operator template gives is not TU-local
// unless the template argument is.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection -freflection-operator-template" }

#include <meta>
#include <type_traits>
using namespace std::meta;

// Canonical variable with external linkage: fine.
struct M1 { int v; consteval operator template() const; };
template<int V> constexpr M1 c1 = {V};
consteval M1::operator template() const { return substitute(^^c1, {reflect_constant(v % 7)}); }
template<M1 m> struct X1 {};
static_assert (std::is_same_v<X1<M1{3}>, X1<M1{10}>>);

// Canonical variable in an unnamed namespace, argument not TU-local: error.
struct M2 { int v; consteval operator template() const; };
namespace { template<int V> constexpr M2 c2 = {V}; }
consteval M2::operator template() const { return substitute(^^c2, {reflect_constant(v % 7)}); }
template<M2 m> struct X2 {};
X2<M2{3}> x2;	// { dg-error "TU-local" }

// A static canonical variable, same.
struct M3 { int v; consteval operator template() const; };
static constexpr M3 c3 = {0};
consteval M3::operator template() const { return ^^c3; }
template<M3 m> struct X3 {};
X3<M3{0}> x3;	// { dg-error "TU-local" }

// The argument's type is TU-local: a TU-local canonical variable is fine.
namespace {
  struct M4 { int v; consteval operator template() const; };
  template<int V> constexpr M4 c4 = {V};
  consteval M4::operator template() const { return substitute(^^c4, {reflect_constant(v % 7)}); }
}
template<M4 m> struct X4 {};
static_assert (std::is_same_v<X4<M4{3}>, X4<M4{10}>>);
