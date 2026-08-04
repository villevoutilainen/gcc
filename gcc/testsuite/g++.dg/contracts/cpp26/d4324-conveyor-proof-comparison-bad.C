// D4324/P2680: -fcontract-conveyor-proofs, comparison-conjunct proof,
// OA_PROVEN_FALSE case -- compute_negative's postcondition establishes a
// range (< 0) that provably violates use_positive's "x > 0" precondition
// for every possible value.  A genuine, confirmed bug the compiler's
// mandatory pass has no way to catch without this flag (it only ever
// recognizes std::is_object_address conjuncts).
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

void use_positive (int x) pre<conveyor_ctrl_v>(x > 0)
{
  (void) x;
}

void caller ()
{
  int r = compute_negative ();
  use_positive (r); // { dg-error "provably violates the precondition" }
}

int main () { caller (); return 0; }
