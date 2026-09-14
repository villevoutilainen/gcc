// Axiom contracts ("the gem", Mechanism B): the user-reported shape
// (an unannotated function's return value passed directly, inline, as
// the argument -- 'consumer (producer ());') -- producer() here has NO
// postcondition at all, so unlike the companion d4324-symbolic-
// runtime-scalar-inline-call-ok.C, there is no established range to
// find via oa_call_symbolic_range_p, and no VAR_DECL/PARM_DECL to key
// a shadow lookup by either. Before the fix, oa_handle_call_symbolic_
// scalar_obligation's own decl-only gate meant this whole dispatch was
// silently skipped entirely -- ctx.check() was never even called, and
// a program built exactly this way (calling an ordinary function
// with an out-of-range return value, wrapped inline in a call to a
// function with a symbolic precondition) would run straight past the
// violation with no diagnostic and no abort at all. Now the dispatch
// happens and correctly finds no established fact, matching d4324-
// symbolic-runtime-scalar-nofact.C's own named-variable case: the
// control object's operator() must see ctx.check() fail (no
// established fact to trust either way), regardless of what the
// actual returned value happens to be.
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

int producer () { return 55; } // no postcondition at all
void consumer (int x) pre<symbolic_ctrl_v>(x >= 20 && x < 1000) { (void) x; }

int main ()
{
  consumer (producer ());
  if (!checked || !failed)
    __builtin_abort ();
  return 0;
}
