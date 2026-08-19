// conveyor_proof_plugin.cc: produce_count()'s postcondition establishes
// this->count in [10,20); consume_count()'s precondition requires
// "this->count < 30" -- the non-literal side is a COMPONENT_REF, not a
// bare PARM_DECL, so oa_match_simple_comparison alone can't recognize
// it; via the plugin's own oa_match_general_comparison_public/oa_
// substitute_call_expr_public query instead (the same fallback the
// built-in checker itself already has), the obligation is discharged
// silently. Previously this shape was invisible to this plugin
// entirely, with no diagnostic at all (see
// .claude/plans/lazy-stirring-pearl.md).
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

struct thing {
  int count;
  void produce_count ()
    post<conveyor_ctrl_v>(this->count >= 10 && this->count < 20)
  { count = 15; }
  void consume_count ()
    pre<conveyor_ctrl_v>(this->count < 30)
  { }
};

int main ()
{
  thing t;
  t.produce_count ();
  t.consume_count ();
  return 0;
}
