// { dg-do run { target c++26 } }

// span is marked with the C++26 "structural" class-head keyword, so
// it can be used directly as a non-type template parameter despite
// its own private data members -- and this must be a pure addition:
// every other observable property stays unchanged.

#include <span>
#include <type_traits>

static_assert( std::is_trivially_copyable_v<std::span<int>> );
static_assert( std::is_standard_layout_v<std::span<int>> );
static_assert( std::is_trivially_copyable_v<std::span<int, 4>> );
static_assert( std::is_standard_layout_v<std::span<int, 4>> );

// The actual new capability: direct NTTP usage, no reflection
// involved, for both a dynamic-extent and a fixed-extent span.
constexpr int arr[] = { 1, 2, 3, 4 };

template<std::span<const int> V>
  constexpr int first () { return V[0]; }
static_assert( first<std::span<const int>(arr)>() == 1 );

template<std::span<const int, 4> V>
  constexpr int last () { return V[3]; }
static_assert( last<std::span<const int, 4>(arr)>() == 4 );

int
main ()
{
  if (first<std::span<const int>(arr)>() != 1)
    __builtin_abort ();
  if (last<std::span<const int, 4>(arr)>() != 4)
    __builtin_abort ();
  return 0;
}
