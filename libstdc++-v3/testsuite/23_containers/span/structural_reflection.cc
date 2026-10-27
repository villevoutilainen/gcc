// { dg-do run { target c++26 } }
// { dg-additional-options "-freflection" }

// With reflection enabled, span is structural by defaulting
// reflect_constant (P4340) rather than with the "structural" class-head
// keyword: the same as structural.cc, which tests it without reflection.

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
