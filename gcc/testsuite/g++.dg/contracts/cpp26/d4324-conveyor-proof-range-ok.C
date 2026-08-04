// D4324/P2680: -fcontract-conveyor-proofs, combined-range subsumption,
// OA_PROVEN_TRUE case -- compute_range's postcondition combines its two
// conjuncts ("r < 100 && r >= 40") into one established interval
// [40, 99], and consume_wide's precondition combines its own two
// conjuncts ("x < 1000 && x >= 20") into one required interval
// [20, 999].  [40, 99] is a subset of [20, 999], so the whole
// precondition is discharged silently -- proven as one combined
// subsumption check, not as two independently-checked conjuncts.
// { dg-do run { target c++26 } }
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

int compute_range () post<conveyor_ctrl_v>(r: r < 100 && r >= 40)
{
  return 50;
}

void consume_wide (int x) pre<conveyor_ctrl_v>(x < 1000 && x >= 20)
{
  (void) x;
}

void caller ()
{
  consume_wide (compute_range ());
}

int main () { caller (); return 0; }
