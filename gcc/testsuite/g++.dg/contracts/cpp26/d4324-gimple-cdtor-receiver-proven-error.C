// D4324: a constructor whose precondition asks to be proven
// (proven_conveyor, proven_symbolic) makes its receiver's
// is_object_address a rule even in ordinary code: an error.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs -fcontract-analysis-engine=gimple" }

#include <contracts>
#include <new>

struct P { int v; P (int n) pre<std::contracts::proven_conveyor_v> (n >= 0) : v (n) {} };
struct Q { int v; Q (int n) pre<std::contracts::proven_symbolic_v> (n >= 0) : v (n) {} };

void make_p (void* mem) { new (mem) P (1); } // { dg-error "cannot prove .is_object_address." }
void make_q (void* mem) { new (mem) Q (1); } // { dg-error "cannot prove .is_object_address." }
