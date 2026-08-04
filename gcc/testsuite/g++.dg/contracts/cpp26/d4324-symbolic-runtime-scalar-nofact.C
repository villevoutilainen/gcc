// Axiom contracts ("the gem", Mechanism B): -fcontract-symbolic-
// runtime-checks, the no-fact case -- consumer() is called with a
// value that was never established via any producer()-shaped call at
// all (a plain literal, assigned directly), so there is no shadow to
// consult; the control object's operator() must see ctx.check() fail
// (no established fact to trust either way).
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
  int y = 55; // never established via any symbolic postcondition
  consumer (y);
  if (!checked || !failed)
    __builtin_abort ();
  return 0;
}
