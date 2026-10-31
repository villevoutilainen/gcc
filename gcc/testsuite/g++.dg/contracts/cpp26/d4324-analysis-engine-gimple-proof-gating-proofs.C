// D4324: with -fcontract-conveyor-proofs too, the GIMPLE engine makes the
// call-site proof checks under -fcontract-analysis-engine=gimple, for
// calls from ordinary and conveyor code alike: a callee's precondition
// whose failed check stops execution is a proof there too, except for its
// is_object_address conjuncts.  See
// d4324-analysis-engine-gimple-proof-gating.C.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple -fcontract-conveyor-proofs" }

#include <contracts>
using namespace std::contracts;

int dv (int n) conveyor pre<conveyor_assert_v> (n != 0) { return 10 / n; }
int rg (int n) conveyor pre<conveyor_assert_v> (n > 3) { return n; }
int oa (int *p) conveyor pre<conveyor_assert_v> (std::is_object_address (p)) { return *p; }

void calls (int x, int *q) conveyor
{
  dv (x); // { dg-warning "cannot verify that .x. is nonzero" }
  rg (x); // { dg-warning "cannot verify that .x. satisfies the precondition" }
  oa (q); // { dg-error "cannot prove .is_object_address. for .q., required by the precondition of" }
}

void plain_calls (int x, int *q)
{
  dv (x); // { dg-warning "cannot verify that .x. is nonzero" }
  rg (x); // { dg-warning "cannot verify that .x. satisfies the precondition" }
  oa (q); // { dg-error "cannot prove .is_object_address. for .q., required by the precondition of" }
}
