// D4324/P2680 item 8, Increment V: the narrow div/mod dataflow check
// now also fires for a postcondition's own condition, scanned against
// a copy of the real, accumulated function-body env -- so a fact
// established by an earlier precondition (via the existing
// precondition-fact-seeding mechanism) is available to a *later*
// postcondition's own div/mod scan. Also exercises the
// oa_nonzero_conjunct_p fix found while implementing this: a bare
// 'm != 0' precondition conjunct on a const-qualified by-value
// parameter (every non-reference postcondition parameter must be
// const) previously failed to be recognized as an nz-fact at all,
// because of an unstripped contract-specific VIEW_CONVERT_EXPR
// const-view wrapper around the parameter.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

struct conveyor_ctrl {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool is_conveyor (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr conveyor_ctrl conveyor_ctrl_v{};

int f (const int m)
  pre<conveyor_ctrl_v>(m != 0)
  post<conveyor_ctrl_v>(r: 10 / m > 0)
{
  return 0;
}

int main () { return f (5); }
