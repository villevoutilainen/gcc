// D4324: under -fcontract-analysis-engine=gimple, the GIMPLE engine's
// call-site proof checks of ordinary code follow -fcontract-conveyor-
// proofs, as the AST engine's do: without it, only the mandatory
// is_object_address obligation is reported.  A call from conveyor code
// is checked the same way: a callee's precondition whose failed check
// stops execution needn't be proven there, only its is_object_address
// conjuncts, which no run-time check can establish.  See also
// d4324-analysis-engine-gimple-proof-gating-proofs.C.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>
using namespace std::contracts;

int dv (int n) conveyor pre<conveyor_assert_v> (n != 0) { return 10 / n; }
int rg (int n) conveyor pre<conveyor_assert_v> (n > 3) { return n; }
int oa (int *p) conveyor pre<conveyor_assert_v> (std::is_object_address (p)) { return *p; }

void calls (int x, int *q) conveyor
{
  dv (x); // { dg-bogus "cannot" }
  rg (x); // { dg-bogus "cannot" }
  oa (q); // { dg-error "cannot prove .is_object_address. for .q., required by the precondition of" }
}

void plain_calls (int x, int *q)
{
  dv (x); // { dg-bogus "cannot verify" }
  rg (x); // { dg-bogus "cannot verify" }
  oa (q); // { dg-error "cannot prove .is_object_address. for .q., required by the precondition of" }
}
