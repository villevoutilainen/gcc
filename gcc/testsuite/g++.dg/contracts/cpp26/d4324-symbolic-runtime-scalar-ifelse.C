// Axiom contracts ("the gem", Mechanism B): -fcontract-symbolic-
// runtime-checks tracking a bare scalar across an if/else join (B2) --
// a shadow created in only one branch must still be visible (via
// oa_env::shadow_decls_merge_with's own union merge) at the consult
// site after the join, on *either* path: proven true when the
// establishing branch actually ran, correctly "cannot verify" when the
// other branch ran instead (its own plain assignment never touches the
// shadow at all, so it stays at its fresh, zero-initialized state).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-runtime-checks" }

#include <contracts>
namespace sc = std::contracts;

bool checked = false;
bool failed = false;

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  {
    checked = true;
    if (!ctx.check ())
      failed = true;
  }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

int producer () post<symbolic_ctrl_v>(r: r >= 40 && r < 100) { return 55; }
void consumer (int x) pre<symbolic_ctrl_v>(x >= 20 && x < 1000) { (void) x; }

void run (bool take_then, int fallback)
{
  int y;
  if (take_then)
    y = producer ();
  else
    y = fallback;
  consumer (y);
}

int main ()
{
  checked = false; failed = false;
  run (true, 0);
  bool then_branch_proven = checked && !failed;

  checked = false; failed = false;
  run (false, 999999);
  bool else_branch_unknown = checked && failed;

  if (!then_branch_proven || !else_branch_unknown)
    __builtin_abort ();
  return 0;
}
