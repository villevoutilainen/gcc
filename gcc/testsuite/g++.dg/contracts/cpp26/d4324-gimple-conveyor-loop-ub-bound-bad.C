// D4324 (GIMPLE engine): a loop's iteration bound must come from its
// exits, not from the undefined behavior a statement in it would have:
// GCC infers that 'r += 1000000000' can't run a third time, since it
// would overflow, and ranger bounded 'r' by that -- proving the overflow
// absent by assuming it.  Each loop below runs three or five times.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>

int three () conveyor
{ int r = 0, i = 0; do { r += 1000000000; ++i; } while (i < 3); return r; } // { dg-error "not provably free of overflow" }
int five () conveyor
{ int r = 0, i = 0; do { r += 1000000000; ++i; } while (i < 5); return r; } // { dg-error "not provably free of overflow" }
