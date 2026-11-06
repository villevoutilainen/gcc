// D4324 (GIMPLE engine): a loop bound proves its counter's increment safe
// only if it leaves room for it: 'i < n' does, 'i <= n' doesn't when n can
// be the type's maximum ('++i' then overflows), and does when n's type is
// narrower (P4445 BUG-19).  Likewise for '--i' with '>' and '>='.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <climits>
#include <contracts>

int le (int n) conveyor
{ int c = 0; for (int i = INT_MAX - 2; i <= n; ++i) c = i; return c; } // { dg-error "increment of 'i' not provably free of overflow" }
int lt (int n) conveyor
{ int c = 0; for (int i = 0; i < n; ++i) c = i; return c; }
int ls (short n) conveyor
{ int c = 0; for (int i = 0; i <= n; ++i) c = i; return c; }
int ge (int n) conveyor
{ int c = 0; for (int i = 10; i >= n; --i) c = i; return c; } // { dg-error "decrement of 'i' not provably free of overflow" }
int gt (int n) conveyor
{ int c = 0; for (int i = 10; i > n; --i) c = i; return c; }
