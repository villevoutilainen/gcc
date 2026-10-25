// D4324: the GIMPLE engine names a comparison, a '!', an '&&'/'||'
// chain and a '?:' with a constant arm as the source spells them,
// rather than as an anonymous temporary ('<unknown>').  An '&&'/'||'
// is a merge of 1 and 0 after a tree of conditions; it is rebuilt from
// those conditions.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <climits>

int cmp (int x) conveyor { return (x > 0) + (INT_MAX - x); }	// { dg-error "result of .\\(\\(\\(int\\)\\(x > 0\\)\\) \\+ \\(2147483647 - x\\)\\)." }
// { dg-error "result of .\\(2147483647 - x\\)." "" { target *-*-* } .-1 }
int conj (int x, int y) conveyor { return (x > 0 && y < 3) + (INT_MAX - x); }	// { dg-error "result of .\\(\\(\\(int\\)\\(\\(x > 0\\) && \\(y <= 2\\)\\)\\) \\+" }
// { dg-error "result of .\\(2147483647 - x\\)." "" { target *-*-* } .-1 }
int disj (int x, int y) conveyor { return (x > 0 || y < 3) + (INT_MAX - x); }	// { dg-error "result of .\\(\\(\\(int\\)\\(\\(x > 0\\) \\|\\| \\(y <= 2\\)\\)\\) \\+" }
// { dg-error "result of .\\(2147483647 - x\\)." "" { target *-*-* } .-1 }
int neg (int x, int y) conveyor { return !(x > 0 && y < 3) + (INT_MAX - x); }	// { dg-error "result of .\\(\\(\\(int\\)\\(\\(x <= 0\\) \\|\\| \\(y > 2\\)\\)\\) \\+" }
// { dg-error "result of .\\(2147483647 - x\\)." "" { target *-*-* } .-1 }
int and_or (int x, int y, int z) conveyor { return ((x > 0 && y < 3) || z == 2) + (INT_MAX - x); }	// { dg-error "result of .\\(\\(\\(int\\)\\(\\(\\(x > 0\\) && \\(y <= 2\\)\\) \\|\\| \\(z == 2\\)\\)\\) \\+" }
// { dg-error "result of .\\(2147483647 - x\\)." "" { target *-*-* } .-1 }
int or_and (int x, int y, int z) conveyor { return ((x > 0 || y < 3) && z == 2) + (INT_MAX - x); }	// { dg-error "result of .\\(\\(\\(int\\)\\(\\(\\(x > 0\\) \\|\\| \\(y <= 2\\)\\) && \\(z == 2\\)\\)\\) \\+" }
// { dg-error "result of .\\(2147483647 - x\\)." "" { target *-*-* } .-1 }
int nested (int x, bool p, bool q) conveyor { return (p && (q || x == 3)) + (INT_MAX - x); }	// { dg-error "result of .\\(\\(\\(int\\)\\(p && \\(q \\|\\| \\(x == 3\\)\\)\\)\\) \\+" }
// { dg-error "result of .\\(2147483647 - x\\)." "" { target *-*-* } .-1 }
int choice (int x, int y) conveyor { return (x > 0 ? y : 4) + (INT_MAX - x); }	// { dg-error "result of .\\(\\(\\(x > 0\\) \\? y : 4\\) \\+" }
// { dg-error "result of .\\(2147483647 - x\\)." "" { target *-*-* } .-1 }
