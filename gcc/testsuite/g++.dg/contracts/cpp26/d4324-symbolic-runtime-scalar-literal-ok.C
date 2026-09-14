// Axiom contracts ("the gem", Mechanism B): -fcontract-symbolic-
// runtime-checks with a LITERAL argument, no intermediate named
// variable at all -- oa_handle_call_symbolic_scalar_obligation used to
// have no case for a bare INTEGER_CST (only a VAR_DECL/PARM_DECL could
// key its shadow map), so the whole runtime dispatch was silently
// skipped for any literal argument, not even reaching the "never
// established, dispatch anyway" fallback. A passing literal (42, well
// within [20,1000)) must genuinely PASS -- treating it the same as an
// unestablished value (see the companion -scalar-nofact.C) would be
// wrong here, since the literal's own value is already fully known.
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

void consumer (int x) pre<symbolic_ctrl_v>(x >= 20 && x < 1000) { (void) x; }

int main ()
{
  consumer (42);
  if (!checked || failed)
    __builtin_abort ();
  return 0;
}
