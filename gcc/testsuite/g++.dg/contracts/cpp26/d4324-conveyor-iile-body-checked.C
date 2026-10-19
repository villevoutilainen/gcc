// D4324: the body of an immediately invoked lambda is part of the
// conveyor function invoking it, and is checked with its facts, whatever
// becomes of its result -- it used to be checked only when an obligation
// on the result walked it.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

int by_ref_parm (int x) conveyor { return [&] () { return 10 / x; } (); }	// { dg-error "divisor .\[^\n\]*x. not provably nonzero" }
int by_value (int x) conveyor { return [x] () { return 10 / x; } (); }	// { dg-error "divisor .\[^\n\]*x. not provably nonzero" }
int zero () conveyor { int x = 0; return [&] () { return 10 / x; } (); }	// { dg-error "divisor .\[^\n\]*x. not provably nonzero" }
int known () conveyor { int x = 5; return [&] () { return 10 / x; } (); }
int known_value () conveyor { int x = 5; return [x] () { return 10 / x; } (); }

