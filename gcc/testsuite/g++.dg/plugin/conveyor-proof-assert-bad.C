// Companion to conveyor-proof-assert-ok.C: OA_PROVEN_FALSE for a plain
// contract_assert statement's own condition -- x's established range
// (< -100, from a preceding if-refinement) provably contradicts the
// asserted 'x > 0'. See .claude/plans/lazy-stirring-pearl.md, Tier 3b.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

void bad_case (int x)
{
  if (x < -100)
    contract_assert<conveyor_ctrl_v> (x > 0); // { dg-error "provably false" }
}

int main () { bad_case (5); return 0; }
