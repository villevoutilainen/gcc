// D4324: a signed division is free of overflow where the conditions
// guarding it exclude a divisor of -1 or a dividend of the minimum,
// including through a constexpr bound (P4445 BUG-10; libstdc++'s
// saturating_div).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <climits>
#include <contracts>

#define P pre<std::contracts::conveyor_assert_v> (y != 0)

int a1 (int x, int y) conveyor P { if (x == INT_MIN) return 0; return x / y; }
int a2 (int x, int y) conveyor P { if (y == -1) return 0; return x / y; }
int a3 (int x, int y) conveyor P
{ if (y == -1) { if (x == INT_MIN) return 0; return x / y; } return x / y; }
long long a4 (long long x, long long y) conveyor P
{ if (y == -1) return 0; return x % y; }

template<class T> struct tr { static constexpr T min = T(1) << (sizeof (T) * 8 - 1); };
long long a5 (long long x, long long y) conveyor P
{ if (y == -1) { if (x == tr<long long>::min) return 0; return x / y; } return x / y; }

// An excluded end of a range narrows it.
unsigned s1 (unsigned a, unsigned n) conveyor
  pre<std::contracts::conveyor_assert_v> (n <= 32)
{ if (n == 32) return 0; return a << n; }
