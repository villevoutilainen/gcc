// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection" }

// tuple defaults reflect_constant (P4340), so tuple<Ts...> is a
// structural type if every Ts is, despite its private bases.

#include <tuple>
#include <string>
#include <type_traits>

static_assert( std::is_structural_v<std::tuple<>> );
static_assert( std::is_structural_v<std::tuple<int, long>> );
static_assert( std::is_structural_v<std::tuple<int, char, long>> );
static_assert( ! std::is_structural_v<std::tuple<std::string>> );
static_assert( ! std::is_structural_v<std::tuple<int, std::string>> );

template<std::tuple<int, char> V> struct S { };
static_assert( std::is_same_v<S<std::tuple<int, char>{1, 'a'}>,
			      S<std::tuple<int, char>{1, 'a'}>> );
static_assert( ! std::is_same_v<S<std::tuple<int, char>{1, 'a'}>,
				S<std::tuple<int, char>{1, 'b'}>> );
static_assert( ! std::is_same_v<S<std::tuple<int, char>{1, 'a'}>,
				S<std::tuple<int, char>{2, 'a'}>> );

template<std::tuple<int, long> V>
  constexpr long sum() { return std::get<0>(V) + std::get<1>(V); }
static_assert( sum<std::tuple<int, long>{2, 3}>() == 5 );
