// Axiom contracts ("the gem", Mechanism B): with -fcontract-symbolic-
// runtime-checks absent, no shadow variable and no establish/consult
// code may be generated at all, even though -fcontract-control-objects
// is on (so oa_walk_stmt still runs, for its other, existing
// purposes) and the source is otherwise identical to
// d4324-symbolic-runtime-scalar-ok.C.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fdump-tree-gimple" }

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

// { dg-final { scan-tree-dump-not "is_valid" "gimple" } }
