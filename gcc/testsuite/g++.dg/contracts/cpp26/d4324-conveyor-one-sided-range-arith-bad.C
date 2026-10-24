// D4324: the counterparts of d4324-conveyor-one-sided-range-arith-ok.C
// that can overflow: a bound from the type doesn't help where the
// known side is the wrong one.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <climits>

int a (int x) conveyor { return INT_MAX - x; }				// { dg-error "not provably free of overflow" }
int b (int x, int y) conveyor { return x + y; }				// { dg-error "not provably free of overflow" }
int c (int x) conveyor { if (x < 0) return INT_MAX - x; return 0; }	// { dg-error "not provably free of overflow" }
long long d (int x) conveyor { if (x > 0) return LLONG_MIN - x; return 0; } // { dg-error "not provably free of overflow" }
