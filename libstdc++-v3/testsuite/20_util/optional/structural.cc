// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection" }

// optional defaults reflect_constant (P4340), so optional<T> is a
// structural type if T is, despite its private bases.

#include <optional>
#include <string>
#include <type_traits>

static_assert( std::is_structural_v<std::optional<int>> );
static_assert( ! std::is_structural_v<std::optional<std::string>> );

template<std::optional<int> V> struct S { };
static_assert( std::is_same_v<S<std::optional<int>{1}>,
			      S<std::optional<int>{1}>> );
static_assert( ! std::is_same_v<S<std::optional<int>{1}>,
				S<std::optional<int>{2}>> );
static_assert( ! std::is_same_v<S<std::optional<int>{}>,
				S<std::optional<int>{0}>> );
static_assert( std::is_same_v<S<std::optional<int>{}>, S<std::nullopt>> );

template<std::optional<int> V>
  constexpr int get() { return V ? *V : -1; }
static_assert( get<std::optional<int>{7}>() == 7 );
static_assert( get<std::nullopt>() == -1 );
