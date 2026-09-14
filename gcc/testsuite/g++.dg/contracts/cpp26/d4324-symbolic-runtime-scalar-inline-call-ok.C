// Axiom contracts ("the gem", Mechanism B): the exact shape d4324-
// symbolic-runtime-scalar-ok.C already covers (producer()'s own
// symbolic postcondition establishes a range consumer()'s precondition
// subsumes), but with producer() called DIRECTLY INLINE as the
// argument -- 'consumer (producer ());' -- instead of first binding its
// result to a named local variable. oa_handle_call_symbolic_scalar_
// obligation used to have no way to consult a callee's own established
// postcondition range for a call-shaped argument, only for a VAR_DECL/
// PARM_DECL already carrying a shadow, so this whole dispatch was
// silently skipped. Now reuses oa_call_symbolic_range_p (the same
// mechanism that already makes the named-variable case work) directly
// on the inline call, so the established range must still be
// recognized and the control object's operator() must still see
// ctx.check() succeed.
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
  consumer (producer ());
  if (!checked || failed)
    __builtin_abort ();
  return 0;
}
