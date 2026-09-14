// Axiom contracts ("the gem", Mechanism B): one more concrete "opaque,
// non-decl argument" shape beyond a bare call result -- a member-access
// read ('consumer (obj.value);') -- confirming the fix generalizes
// beyond literals and call results specifically. obj.value has never
// been established via any producer()-shaped postcondition, and is
// itself a COMPONENT_REF, neither a literal nor a VAR_DECL/PARM_DECL,
// so it takes the same "never established, dispatch anyway" fallback
// path a plain unestablished named variable does (d4324-symbolic-
// runtime-scalar-nofact.C) -- the control object's operator() must see
// ctx.check() fail, regardless of obj.value's own actual (here,
// perfectly in-range) contents.
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

struct Holder { int value; };

int main ()
{
  Holder obj{55}; // in-range, but never established via any postcondition
  consumer (obj.value);
  if (!checked || !failed)
    __builtin_abort ();
  return 0;
}
