// gimple_object_address_plugin.cc: ptr->field range facts for a
// *persistent object* -- produce()'s own postcondition establishes
// this->count in [40, 100) via the same predicate_dom_walker forward
// dataflow named-predicate facts use (keyed by (identity, FIELD_DECL)
// instead of identity alone), consulted by consume()'s own precondition
// requiring this->count in [20, 1000) on a later, separate call.
// Checked through both a plain-object receiver ('t.produce()') and a
// pointer receiver ('tp->produce()'), confirming both identity-
// resolution paths, same as the named-predicate case. Field-range
// shape recognition uses the newly-exported oa_match_field_range_
// comparison/oa_strip_symbolic_ptr_expr_public directly, bypassing
// oa_precondition_field_range_obligations's own gate (found, by direct
// testing, to answer false at GIMPLE-pass time for a plainly conveyor-
// active contract) -- see ~/gimple-contract-analysis.md.
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
namespace sc = std::contracts;

struct conveyor_ctrl {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool is_conveyor (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr conveyor_ctrl conveyor_ctrl_v{};

struct thing {
  int count;
  void produce () post<conveyor_ctrl_v>(this->count >= 40 && this->count < 100)
  { count = 55; }
  void consume () pre<conveyor_ctrl_v>(this->count >= 20 && this->count < 1000)
  { }
};

void dot_receiver ()
{
  thing t;
  t.produce ();
  t.consume ();
}

void pointer_receiver ()
{
  thing t;
  thing *tp = &t;
  tp->produce ();
  tp->consume ();
}

int main ()
{
  dot_receiver ();
  pointer_receiver ();
  return 0;
}
