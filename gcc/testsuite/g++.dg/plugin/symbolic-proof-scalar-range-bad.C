// symbolic_proof_plugin.cc: producer_bad's postcondition establishes
// [200,300) for its own return value, fully disjoint from consumer's
// required [20,100) -- a genuine, provable violation.  See
// .claude/plans/well-we-last-discussed-ethereal-duckling.md.
// { dg-do compile }
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

int producer_bad () post<symbolic_ctrl_v>(r: r >= 200 && r < 300) { return 250; }
void consumer (int x) pre<symbolic_ctrl_v>(x >= 20 && x < 100) { (void) x; }

void caller ()
{
  int y = producer_bad ();
  consumer (y); // { dg-error "provably violates the precondition" }
}

int main () { caller (); return 0; }
