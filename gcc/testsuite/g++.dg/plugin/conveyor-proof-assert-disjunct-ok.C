// conveyor_proof_plugin.cc: a disjunctive contract_assert condition
// ('x > 0 || x < -1000') proven true because its FIRST disjunct alone
// is independently provable (x's established range, > 100 from a
// preceding if-refinement) -- via the new oa_collect_disjuncts_public
// export, tried against each disjunct through oa_check_assertion_
// conjunct_public. Previously a top-level '||' was not recognized at
// all anywhere in the plugin API. See .claude/plans/lazy-stirring-
// pearl.md, Tier 3b.
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

void disjunct_ok (int x)
{
  if (x > 100)
    contract_assert<conveyor_ctrl_v> (x > 0 || x < -1000);
}

int main () { disjunct_ok (200); return 0; }
