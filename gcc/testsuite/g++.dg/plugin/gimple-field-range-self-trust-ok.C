// gimple_object_address_plugin.cc: the self-trust case for field-range
// facts -- g's own declared precondition "p->count >= 20 && p->count <
// 1000" is trusted for the rest of g's own body (seed_predicate_self_
// trust's own field-range collection seeds ssa_default_def(g, p) paired
// with the FIELD_DECL into the dominator walk's own root/seed state),
// so the consume() call inside g's own body is discharged purely from
// that seeded fact. See ~/gimple-contract-analysis.md.
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
  void produce ()
    pre<conveyor_ctrl_v>(std::is_object_address (this))
    post<conveyor_ctrl_v>(this->count >= 40 && this->count < 100)
  { count = 55; }
  void consume ()
    pre<conveyor_ctrl_v>(std::is_object_address (this))
    pre<conveyor_ctrl_v>(this->count >= 20 && this->count < 1000)
  { }
};

void g (thing *p)
  pre<conveyor_ctrl_v>(std::is_object_address (p))
  pre<conveyor_ctrl_v>(p->count >= 20 && p->count < 1000)
{
  p->consume ();
}

int main ()
{
  thing t;
  t.produce ();
  g (&t);
  return 0;
}
