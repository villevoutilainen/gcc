// conveyor_proof_plugin.cc: produce_count()'s postcondition establishes
// this->count in [40,100); consume_count()'s precondition requires
// this->count in [20,1000) on the same object, with nothing
// invalidating it in between -- [40,100) is a subset of [20,1000), so
// the obligation is discharged silently, via the plugin's own query
// against the real fact engine (m_contract_field_range_map, shared
// with symbolic_proof_plugin.cc's own field-range check).  See
// .claude/plans/well-we-last-discussed-ethereal-duckling.md.
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

struct thing {
  int count;
  void produce_count ()
    post<conveyor_ctrl_v>(this->count >= 40 && this->count < 100)
  { count = 55; }
  void consume_count ()
    pre<conveyor_ctrl_v>(this->count >= 20 && this->count < 1000)
  { }
};

int main ()
{
  thing t;
  t.produce_count ();
  t.consume_count ();
  return 0;
}
