// symbolic_proof_plugin.cc: producer's postcondition establishes
// [40,100) for its own return value; consumer's precondition requires
// [20,1000) for its by-value parameter.  y = producer(); consumer(y);
// gives oa_object_identity_decl a stable key (y) to track the
// established range by -- [40,100) is fully subsumed by [20,1000), so
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

int producer () post<symbolic_ctrl_v>(r: r >= 40 && r < 100) { return 55; }
void consumer (int x) pre<symbolic_ctrl_v>(x >= 20 && x < 1000) { (void) x; }

int main ()
{
  int y = producer ();
  consumer (y);
  return 0;
}
