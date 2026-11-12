// D4324: a counted loop's accumulator stays within what the loop's
// trip count allows ('t += i' over ten iterations), from the counter's
// start, step and bound, so it is provably free of overflow.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

using std::contracts::conveyor_assert_v;

// P4445 2.2.2.9: a loop with a known bound adds up a bounded amount.
int sum10 () conveyor
{ int t = 0; for (int i = 0; i < 10; ++i) t += i; return t; }
int factorial (int n) conveyor pre<conveyor_assert_v> (n >= 0 && n <= 10)
{ int r = 1; for (int i = 2; i <= n; ++i) r *= i; return r; }
int fact12 () conveyor
{ int r = 1; for (int i = 2; i <= 12; ++i) r *= i; return r; }
int counter () conveyor
{ int c = 0; for (int i = 0; i < 100; ++i) ++c; return c; }
int down () conveyor
{ int t = 0; for (int i = 10; i > 0; --i) t -= i; return t; }
// The last iteration adds 1e9 to 1e9: the loop does not run a third time.
int two () conveyor
{ int r = 0; for (int i = 0; i < 2; ++i) r += 1000000000; return r; }
int ne () conveyor
{ int t = 0; for (int i = 0; i != 50; ++i) t += 40000000; return t; }
int step2 () conveyor
{ int t = 0; for (int i = 0; i < 6; i += 2) t += 700000000; return t; }
int cond () conveyor
{ int c = 0; for (int i = 0; i < 100; ++i) if (i % 3 == 0) ++c; return c; }
int brk (int k) conveyor
{ int t = 0; for (int i = 0; i < 10; ++i) { if (i == k) break; t += 100000000; } return t * 2; }
int cont (int k) conveyor
{ int t = 0; for (int i = 0; i < 4; ++i) { if (i == k) continue; t += 500000000; } return t; }
int after () conveyor
{ int t = 0; for (int i = 0; i < 3; ++i) t += 700000000; return t + 40000000; }
int exit_value () conveyor
{ int i = 0; for (; i < 10; ++i) {} return i * 200000000; }
// Nested counted loops.
int nested () conveyor
{ int t = 0; for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) t += 200000000; return t; }
