// D4324: a precondition whose control object asks to be proven --
// proven_conveyor (strict: unprovable is an error) or analyzed_conveyor
// (lenient: a warning) -- is owed at every call, without
// -fcontract-conveyor-proofs: from conveyor code too, where a precondition
// whose failed check stops execution is otherwise owed only as a proof,
// and from a strict predicate's text.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>
using namespace std::contracts;

int pv (int i) conveyor pre<proven_conveyor_v> (i > 0);
int av (int i) conveyor pre<analyzed_conveyor_v> (i > 0);

int body_p (int i) conveyor { return pv (i); }	// { dg-error "cannot prove that .i. satisfies" }
int body_a (int i) conveyor { return av (i); }	// { dg-warning "cannot verify that .i. satisfies" }
int ord_p (int i) { return pv (i); }		// { dg-error "cannot prove that .i. satisfies" }
int ord_a (int i) { return av (i); }		// { dg-warning "cannot verify that .i. satisfies" }
void pred_p (int i) pre<conveyor_assert_v> (pv (i) > 0) {}	// { dg-error "cannot prove that .i. satisfies" }
void pred_a (int i) pre<conveyor_assert_v> (av (i) > 0) {}	// { dg-warning "cannot verify that .i. satisfies" }
int ok_p (int i) conveyor { return i > 0 ? pv (i) : 0; }
void pred_ok (int i) pre<conveyor_assert_v> (i > 0 && pv (i) > 0) {}
