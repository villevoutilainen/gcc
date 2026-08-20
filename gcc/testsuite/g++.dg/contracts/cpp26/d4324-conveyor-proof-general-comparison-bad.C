// D4324/P2680: -fcontract-conveyor-proofs, the general-comparison
// fallback (oa_match_general_comparison, for a conjunct shaped as
// "EXPR OP <literal>" where EXPR isn't a bare parameter -- here a
// value-preserving cast, '(long) x > 0') hitting OA_PROVEN_FALSE, not
// just the bare-parameter range_parms case d4324-conveyor-proof-
// comparison-bad.C already covers. Exercises the established-fact
// follow-up note's own general-comparison branch specifically (see
// .claude/plans/lazy-stirring-pearl.md).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

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

int compute_negative () post<conveyor_ctrl_v>(r: r < 0)
{
  return -1;
}

void use_positive_long (int x) pre<conveyor_ctrl_v>((long) x > 0)
{
  (void) x;
}

void caller ()
{
  int r = compute_negative ();
  use_positive_long (r); // { dg-error "provably violates the precondition" }
                         // { dg-message "is established \[^\n\]*, but the precondition requires" "established fact" { target *-*-* } .-1 }
}

int main () { caller (); return 0; }
