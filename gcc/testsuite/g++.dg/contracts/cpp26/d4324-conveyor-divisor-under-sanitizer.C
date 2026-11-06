// D4324: -fsanitize= instrumentation doesn't change what a conveyor
// function may do: a guarded division is still proven, an unguarded one
// still rejected (P4445 BUG-24, where the instrumentation's SAVE_EXPRs hid
// the guard's fact).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fsanitize=undefined -fcontract-analysis-engine=ast" }

#include <contracts>

int quot (int x, int y) conveyor { return x / y; } // { dg-error "divisor 'y' not provably nonzero" }
int quot2 (int x, int y) conveyor { if (y != 0) return x / y; return 0; }
