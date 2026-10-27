// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection" }

// variant defaults reflect_constant (P4340), so variant<Ts...> is a
// structural type if every Ts is, despite its private bases.

#include <variant>
#include <string>
#include <type_traits>

using V = std::variant<int, long>;

static_assert( std::is_structural_v<V> );
static_assert( std::is_structural_v<std::variant<std::monostate, int>> );
static_assert( ! std::is_structural_v<std::variant<int, std::string>> );

template<V X> struct S { };
static_assert( std::is_same_v<S<V{1}>, S<V{1}>> );
static_assert( ! std::is_same_v<S<V{1}>, S<V{2}>> );
// Equivalence goes by the active alternative too.
static_assert( ! std::is_same_v<S<V{0}>, S<V{0L}>> );

template<V X>
  constexpr long get()
  { return X.index() == 0 ? std::get<0>(X) : std::get<1>(X) + 100; }
static_assert( get<V{5}>() == 5 );
static_assert( get<V{5L}>() == 105 );
