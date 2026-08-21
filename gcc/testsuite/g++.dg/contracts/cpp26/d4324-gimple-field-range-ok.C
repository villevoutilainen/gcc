// The built-in GIMPLE-pass engine's own ptr->field range check for a
// *persistent object* (see gcc/cp/contracts-gimple.cc and
// ~/gimple-contract-analysis.md), gated by
// -fcontract-conveyor-proofs-gimple: produce()'s own postcondition
// establishes this->count in [40, 100) via the same
// cg_predicate_dom_walker forward dataflow named-predicate facts use
// (keyed by (identity, FIELD_DECL) instead of identity alone),
// consulted by consume()'s own precondition requiring this->count in
// [20, 1000) on a later, separate call. Checked through both a
// plain-object receiver ('t.produce()') and a pointer receiver
// ('tp->produce()').
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple" }
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
  void produce ()
    pre<conveyor_ctrl_v>(std::is_object_address (this))
    post<conveyor_ctrl_v>(this->count >= 40 && this->count < 100)
  { count = 55; }
  void consume ()
    pre<conveyor_ctrl_v>(std::is_object_address (this))
    pre<conveyor_ctrl_v>(this->count >= 20 && this->count < 1000)
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
