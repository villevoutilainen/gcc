// D4324 (GIMPLE engine): in a conveyor function, a signed division or
// remainder must also provably not be the type's minimum divided by -1,
// which overflows: the divisor's range must leave out -1, or the
// dividend's the minimum (P4445 BUG-10).  A nonzero divisor alone isn't
// enough.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <climits>
#include <contracts>

int q1 (int x, int y) conveyor { if (y != 0) return x / y; return 0; } // { dg-error "division of 'x' by 'y' not provably free of overflow" }
int q2 (int x, int y) conveyor { if (y > 0) return x / y; return 0; }
int q3 (int x, int y) conveyor { if (y != 0 && x > INT_MIN) return x / y; return 0; }
int q4 (int x) conveyor { return x / 2; }
int q5 (int x, int y) conveyor { if (y != 0) return x % y; return 0; } // { dg-error "remainder of 'x' by 'y' not provably free of overflow" }
unsigned q6 (unsigned x, unsigned y) conveyor { if (y != 0) return x / y; return 0; }
// GIMPLE folds 'x / -1' to '-x', whose own check reports it.
int q7 (int x) conveyor { return x / -1; } // { dg-error "not provably free of overflow" }
