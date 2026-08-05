// symbolic_proof_plugin.cc: produce_count()'s postcondition establishes
// this->count in [40,100); consume_count()'s precondition requires
// this->count in [20,1000) on the same object, with nothing
// invalidating it in between -- [40,100) is a subset of [20,1000), so
// the obligation is discharged silently, via the plugin's own query
// against the real fact engine.  See .claude/plans/well-we-last-
// discussed-ethereal-duckling.md.
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
namespace sc = std::contracts;

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

struct thing {
  int count;
  void produce_count ()
    post<symbolic_ctrl_v>(this->count >= 40 && this->count < 100)
  { count = 55; }
  void consume_count ()
    pre<symbolic_ctrl_v>(this->count >= 20 && this->count < 1000)
  { }
};

int main ()
{
  thing t;
  t.produce_count ();
  t.consume_count ();
  return 0;
}
