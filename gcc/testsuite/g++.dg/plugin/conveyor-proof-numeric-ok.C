// conveyor_proof_plugin.cc: OA_PROVEN_TRUE for a plain comparison
// conjunct -- r's established range (> 0, from compute_positive's
// postcondition) provably satisfies use_positive's "x > 0"
// precondition.  See .claude/plans/stateless-jumping-shore.md.
// { dg-do run }
// { dg-additional-sources "conveyor-proof-defs.cc" }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

void caller ()
{
  int r = compute_positive ();
  use_positive (r);
}

int main () { caller (); return 0; }
