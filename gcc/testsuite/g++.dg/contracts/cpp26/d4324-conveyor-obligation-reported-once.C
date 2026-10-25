// D4324: an unprovable call-site obligation is reported once per call,
// even when several preconditions require it: an explicit
// is_object_address precondition on a conveyor function's reference
// parameter besides the implicit one it gets, or a repeated precondition.
// Previously each requiring precondition reported it again.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

namespace sc = std::contracts;

int f (const int& t) conveyor pre<sc::never_proven_conveyor_v> (std::is_object_address (&t));
int g (const int& t) conveyor;
int h (const int& t) conveyor pre<sc::conveyor_assert_v> (std::is_object_address (&t));
int d (int a, int b) conveyor pre<sc::analyzed_conveyor_v> (b != 0) pre<sc::analyzed_conveyor_v> (b != 0);

// F's never_proven precondition claims what its implicit one would
// require: a never_proven assertion needs no proof of its input facts.
int use_f (const int& s) { return f (s); }
int use_g (const int& s) { return g (s); }	// { dg-error "cannot prove .is_object_address." }
int use_h (const int& s) { return h (s); }	// { dg-error "cannot prove .is_object_address." }
int use_d (int x) { return d (1, x); }		// { dg-warning "cannot verify that .x. is nonzero" }
