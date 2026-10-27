// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection" }

// expected defaults reflect_constant (P4340), so expected<T, E> is a
// structural type if T (unless void) and E are, despite its private
// members.

#include <expected>
#include <string>
#include <type_traits>

using E = std::expected<int, long>;
using EV = std::expected<void, int>;

static_assert( std::is_structural_v<E> );
static_assert( std::is_structural_v<EV> );
static_assert( ! std::is_structural_v<std::expected<std::string, int>> );
static_assert( ! std::is_structural_v<std::expected<int, std::string>> );
static_assert( ! std::is_structural_v<std::expected<void, std::string>> );

template<E X> struct S { };
static_assert( std::is_same_v<S<E{3}>, S<E{3}>> );
static_assert( ! std::is_same_v<S<E{3}>, S<E{4}>> );
static_assert( ! std::is_same_v<S<E{3}>, S<E{std::unexpect, 3L}>> );
static_assert( ! std::is_same_v<S<E{std::unexpect, 1L}>,
				S<E{std::unexpect, 2L}>> );

template<EV X> struct SV { };
static_assert( std::is_same_v<SV<EV{}>, SV<EV{}>> );
static_assert( ! std::is_same_v<SV<EV{}>, SV<EV{std::unexpect, 0}>> );

template<E X>
  constexpr int val() { return X ? *X : -int(X.error()); }
static_assert( val<E{4}>() == 4 );
static_assert( val<E{std::unexpect, 4L}>() == -4 );
