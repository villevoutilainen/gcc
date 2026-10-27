// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection" }
// { dg-require-normal-mode "" }

// inplace_vector defaults reflect_constant (P4340), so inplace_vector<T, N>
// is a structural type if T is, despite its private members.

#include <inplace_vector>
#include <meta>
#include <string>
#include <type_traits>

using IV = std::inplace_vector<int, 4>;

static_assert( std::is_structural_v<IV> );
static_assert( ! std::is_structural_v<std::inplace_vector<std::string, 4>> );
static_assert( std::is_structural_v<std::inplace_vector<int, 0>> );
static_assert( std::is_structural_v<std::inplace_vector<std::string, 0>> );

template<IV V> struct S { };
static_assert( std::is_same_v<S<IV{1, 2}>, S<IV{1, 2}>> );
static_assert( ! std::is_same_v<S<IV{1, 2}>, S<IV{1, 3}>> );
static_assert( ! std::is_same_v<S<IV{1, 2}>, S<IV{1, 2, 0}>> );

template<IV V>
  constexpr int sum() { int s = 0; for (int x : V) s += x; return s; }
static_assert( sum<IV{1, 2, 3}>() == 6 );

consteval std::span<const IV> arr()
{
  std::vector<IV> v{IV{1}, IV{2, 3}, IV{}};
  return std::define_static_array(v);
}
static_assert( arr().size() == 3 );
static_assert( arr()[1].size() == 2 && arr()[1][1] == 3 );
static_assert( arr()[2].empty() );
