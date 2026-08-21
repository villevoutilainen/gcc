// conveyor_proof_plugin.cc: produce_value_bad()'s postcondition
// establishes this->value in [200.0,300.0), fully disjoint from
// consume_value()'s required [20.0,100.0) -- a genuine, provable
// violation, via the plugin's own float field-range query.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

struct thing {
  double value;
  void produce_value_bad ()
    pre<conveyor_ctrl_v>(std::is_object_address (this))
    post<conveyor_ctrl_v>(this->value >= 200.0 && this->value < 300.0)
  { value = 250.0; }
  void consume_value ()
    pre<conveyor_ctrl_v>(std::is_object_address (this))
    pre<conveyor_ctrl_v>(this->value >= 20.0 && this->value < 100.0)
  { }
};

void caller ()
{
  thing t;
  t.produce_value_bad ();
  t.consume_value (); // { dg-error "provably violates the precondition" }
}

int main () { caller (); return 0; }
