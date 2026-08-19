// conveyor_proof_plugin.cc: OA_PROVEN_TRUE for a plain contract_assert
// statement's own condition -- 'x > 0', with x's established range
// (> 100, from a preceding if-refinement) provably satisfying it. This
// shape was previously invisible to this plugin entirely (it only ever
// observed CALL_EXPR/AGGR_INIT_EXPR sites, never a contract_assert),
// via oa_walk_function_calls's new ASSERT_CALLBACK parameter and the
// new oa_check_assertion_conjunct_public export. See
// .claude/plans/lazy-stirring-pearl.md, Tier 3b.
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

void ok_case (int x)
{
  if (x > 100)
    contract_assert<conveyor_ctrl_v> (x > 0);
}

int main () { ok_case (200); return 0; }
