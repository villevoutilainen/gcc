// Axiom contracts ("the gem", Mechanism B, ~/gcc-axiom-contracts.md and
// .claude/plans/stateless-jumping-shore.md): -fcontract-symbolic-
// runtime-checks tracking a bare by-value scalar (no enclosing pointer/
// object at all) across *separate statements* within one function --
// producer()'s postcondition establishes a range for its own return-
// value binder; consumer()'s precondition requires a subsuming range on
// its own by-value parameter. Straight-line only (B1): established
// [40,100) subsumed by required [20,1000), so the control object's
// operator() must see ctx.check() succeed.
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

int main ()
{
  int y = producer ();
  consumer (y);
  if (!checked || failed)
    __builtin_abort ();
  return 0;
}
