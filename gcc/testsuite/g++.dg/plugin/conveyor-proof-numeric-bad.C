// conveyor_proof_plugin.cc: OA_PROVEN_FALSE for a plain comparison
// conjunct -- r's established range (< 0, from compute_negative's
// postcondition) provably fails use_positive's "x > 0" precondition
// for every possible value.  A genuine, confirmed bug the compiler's
// own mandatory pass has no way to catch (it only ever recognizes
// std::is_object_address conjuncts) -- see
// .claude/plans/stateless-jumping-shore.md.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

void caller2 ()
{
  int r = compute_negative ();
  use_positive (r); // { dg-error "provably violates the precondition" }
}

int main () { caller2 (); return 0; }
