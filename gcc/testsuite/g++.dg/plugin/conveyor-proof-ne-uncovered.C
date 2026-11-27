// conveyor_proof_plugin.cc checks a callee's 'x != C' precondition, and
// reports a conjunct it has no proof rule for (here 'x != y') rather than
// passing over it, as the built-in -fcontract-conveyor-proofs does: a
// constant the arguments fold it to decides it.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

void ne1 (int x) pre<conveyor_ctrl_v> (x != 1);
void differ (int x, int y) pre<conveyor_ctrl_v> (x != y);

void ne_ok (int k) { if (k > 1) ne1 (k); }
void ne_unknown (int k) { ne1 (k); }	// { dg-warning "cannot verify that .k. satisfies" }
void ne_bad () { ne1 (1); }		// { dg-error "provably violates the precondition" }
void ne_alias (int k) { if (k != 1) { int *p = &k; *p = 1; ne1 (k); } } // { dg-warning "cannot verify that .k. satisfies" }
void uncovered (int a, int b) { differ (a, b); }	// { dg-warning "cannot verify the precondition" }
void uncovered_folded () { differ (1, 2); }
void uncovered_bad () { differ (3, 3); }	// { dg-error "the arguments provably violate the precondition" }

int main () { return 0; }
