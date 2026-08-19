// Companion to conveyor-proof-assert-disjunct-ok.C: a disjunctive
// contract_assert condition ('x > 100 || x < -100') proven false
// because BOTH disjuncts are independently provable false (x's
// established range, [1, 10] from a preceding if-refinement, rules out
// each one on its own) -- the combining rule's own FALSE-dominates
// case: PROVEN_FALSE only when every disjunct is independently
// provable false. See .claude/plans/lazy-stirring-pearl.md, Tier 3b.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

void disjunct_bad (int x)
{
  if (x > 0 && x < 11)
    contract_assert<conveyor_ctrl_v> (x > 100 || x < -100); // { dg-error "provably false" }
}

int main () { disjunct_bad (5); return 0; }
