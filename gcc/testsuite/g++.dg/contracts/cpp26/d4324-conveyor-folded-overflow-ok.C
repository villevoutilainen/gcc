// D4324: the counterparts of d4324-conveyor-folded-overflow-bad.C whose
// operation is guarded, through '&&' and '||' too: these must stay
// short-circuit when the operation is kept, not evaluate it
// unconditionally.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <climits>

bool a (int x) conveyor { return x > 0 && INT_MAX - x > 5; }
bool b (int x) conveyor { return x <= 0 || INT_MAX - x > 5; }
bool c (int x) conveyor { return x < INT_MAX && x + 1 > x; }
int d (int x) conveyor { if (x > -1000 && x < 1000) return (x * 2) / 2; return 0; }
