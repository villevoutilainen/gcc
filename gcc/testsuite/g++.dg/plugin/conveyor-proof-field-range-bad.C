// conveyor_proof_plugin.cc: produce_count_bad()'s postcondition
// establishes this->count in [200,300), fully disjoint from
// consume_count()'s required [20,100) -- a genuine, provable
// violation, via the plugin's own query against the real fact engine.
// See .claude/plans/well-we-last-discussed-ethereal-duckling.md.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

struct thing {
  int count;
  void produce_count_bad ()
    pre<conveyor_ctrl_v>(std::is_object_address (this))
    post<conveyor_ctrl_v>(this->count >= 200 && this->count < 300)
  { count = 250; }
  void consume_count ()
    pre<conveyor_ctrl_v>(std::is_object_address (this))
    pre<conveyor_ctrl_v>(this->count >= 20 && this->count < 100)
  { }
};

void caller ()
{
  thing t;
  t.produce_count_bad ();
  t.consume_count (); // { dg-error "provably violates the precondition" }
}

int main () { caller (); return 0; }
