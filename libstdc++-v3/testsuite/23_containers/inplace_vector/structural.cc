// { dg-do run { target c++26 } }
// { dg-additional-options "-freflection" }
// { dg-require-normal-mode "" }

// inplace_vector defaults reflect_constant (P4340), so inplace_vector<T, N>
// is a structural type if T is, despite its private members.

#include <inplace_vector>
#include <meta>
#include <string>
#include <type_traits>
#include <vector>
#include <testsuite_hooks.h>

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

// Elements removed during constant evaluation are no longer within their
// lifetime, so they don't take part in template argument identity.
consteval IV popped() { IV v{1, 2, 3}; v.pop_back(); return v; }
consteval IV erased() { IV v{1, 9, 2}; v.erase(v.begin() + 1); return v; }
consteval IV resized() { IV v{1, 2, 7, 8}; v.resize(2); return v; }
consteval IV refilled() { IV v{1, 7}; v.pop_back(); v.push_back(2); return v; }
static_assert( std::is_same_v<S<IV{1, 2}>, S<popped()>> );
static_assert( std::is_same_v<S<IV{1, 2}>, S<erased()>> );
static_assert( std::is_same_v<S<IV{1, 2}>, S<resized()>> );
static_assert( std::is_same_v<S<IV{1, 2}>, S<refilled()>> );

consteval std::span<const IV> arr()
{
  std::vector<IV> v{IV{1}, IV{2, 3}, IV{}, popped(), erased()};
  return std::define_static_array(v);
}
static_assert( arr().size() == 5 );
static_assert( arr()[1].size() == 2 && arr()[1][1] == 3 );
static_assert( arr()[2].empty() );
static_assert( arr()[3] == IV{1, 2} );

int main()
{
  constexpr std::span<const IV> a = arr();
  int sum = 0;
  for (const IV& v : a)
    for (int x : v)
      sum += x;
  VERIFY( sum == 1 + 2 + 3 + 1 + 2 + 1 + 2 );
  VERIFY( a[4] == IV({1, 2}) );
}
