// D4324/Mechanism B ("the gem", -fcontract-symbolic-runtime-checks):
// oa_handle_call_symbolic_scalar_obligation's own arg_decl resolution
// now goes through oa_strip_conversion_call (matching oa_get_range's
// own, already-established practice) instead of STRIP_ANY_LOCATION_
// WRAPPER alone -- ARG_DECL is only ever used as a lookup key into
// ENV's own runtime shadow map, so a by-value scalar argument reaching
// consumer()'s own precondition through an *implicit widening
// conversion* (int -> long, a NOP_EXPR/CONVERT_EXPR STRIP_ANY_LOCATION_
// WRAPPER alone never strips) must still resolve down to producer()'s
// own established shadow. Confirmed by direct testing that, before this
// fix, the unresolved NOP_EXPR never matched any shadow-map key, so the
// generated dispatch silently fell into the "no shadow" branch and
// *failed* the runtime check (a spurious trap) despite the value
// genuinely being in range -- checked and failed here would have been
// true and false respectively. See .claude/plans/well-we-last-
// discussed-ethereal-duckling.md.
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
void consumer (long x) pre<symbolic_ctrl_v>(x >= 20 && x < 1000) { (void) x; }

int main ()
{
  int y = producer ();
  consumer (y);
  if (!checked || failed)
    __builtin_abort ();
  return 0;
}
