// D4324: the GIMPLE engine names a value the way the AST engine does --
// the variable, parameter or field it was read from, or the arithmetic
// that computed it -- rather than an anonymous temporary ('<unknown>'),
// and reports '++x' as an increment of x.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>

struct M { int v; };

int field_ref (const M &r, int x) conveyor { return x / r.v; } // { dg-error "divisor .r.M::v. not provably nonzero" }
int local (int x) conveyor { int i = x; return 7 / i; } // { dg-error "divisor .i. not provably nonzero" }
int sum (int x, int y) conveyor { return x - y; } // { dg-error "result of .\\(x - y\\). not provably free of overflow" }
int incr (int x) conveyor { ++x; return x; } // { dg-error "increment of .x. not provably free of overflow" }
int field_ptr (const M *p, int x) conveyor { return x / p->v; } // { dg-error "divisor .p->M::v. not provably nonzero" }
// { dg-error "pointer dereference of .p. not provably valid" "" { target *-*-* } .-1 }
