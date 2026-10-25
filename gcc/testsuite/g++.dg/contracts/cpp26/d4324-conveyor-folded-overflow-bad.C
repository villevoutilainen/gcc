// D4324: an overflowing operation is checked even where folding could
// erase it.  'INT_MAX - x > 5' folds to 'x <= 2147483641' (signed
// overflow being undefined), which the GIMPLE engine used to see
// instead, accepting all of these.  A checked function is now folded
// and gimplified as if overflow trapped, keeping the operation.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <climits>

bool a (int x) conveyor { return INT_MAX - x > 5; }		// { dg-error "not provably free of overflow" }
bool b (int x) conveyor { return x > 0 || INT_MAX - x > 5; }	// { dg-error "not provably free of overflow" }
bool c (int x) conveyor { return x + 1 > x; }			// { dg-error "not provably free of overflow" }
int d (int x) conveyor { return (x * 2) / 2; }			// { dg-error "not provably free of overflow" }
