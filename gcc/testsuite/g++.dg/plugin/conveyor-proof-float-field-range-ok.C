// conveyor_proof_plugin.cc: produce_value()'s postcondition establishes
// this->value in [0.0,10.0); consume_value()'s precondition requires
// this->value in [-5.0,20.0) on the same object -- [0.0,10.0) is a
// subset of [-5.0,20.0), so the obligation is discharged silently, via
// the plugin's own oa_precondition_float_field_range_obligations/
// oa_env_check_float_field_range_fact query (previously this plugin had
// no way at all to observe a float-bounded field precondition
// conjunct -- see .claude/plans/lazy-stirring-pearl.md).
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

struct thing {
  double value;
  void produce_value ()
    pre<conveyor_ctrl_v>(std::is_object_address (this))
    post<conveyor_ctrl_v>(this->value >= 0.0 && this->value < 10.0)
  { value = 5.5; }
  void consume_value ()
    pre<conveyor_ctrl_v>(std::is_object_address (this))
    pre<conveyor_ctrl_v>(this->value >= -5.0 && this->value < 20.0)
  { }
};

int main ()
{
  thing t;
  t.produce_value ();
  t.consume_value ();
  return 0;
}
