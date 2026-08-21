// conveyor_proof_plugin.cc: produce_count_bad()'s postcondition
// establishes this->count in [30,40), so "this->count < 30" is provably
// false -- a genuine violation of the general-comparison-shaped
// precondition, via the plugin's own oa_match_general_comparison_public
// query.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

struct thing {
  int count;
  void produce_count_bad ()
    pre<conveyor_ctrl_v>(std::is_object_address (this))
    post<conveyor_ctrl_v>(this->count >= 30 && this->count < 40)
  { count = 35; }
  void consume_count ()
    pre<conveyor_ctrl_v>(std::is_object_address (this))
    pre<conveyor_ctrl_v>(this->count < 30)
  { }
};

void caller ()
{
  thing t;
  t.produce_count_bad ();
  t.consume_count (); // { dg-error "provably violates the precondition" }
}

int main () { caller (); return 0; }
