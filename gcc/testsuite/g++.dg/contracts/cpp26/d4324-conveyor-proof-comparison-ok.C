// D4324/P2680: -fcontract-conveyor-proofs, comparison-conjunct proof,
// OA_PROVEN_TRUE case -- compute_positive's postcondition establishes a
// range that provably satisfies use_positive's plain comparison-shaped
// precondition conjunct (not std::is_object_address, which the compiler
// already checks mandatorily without this flag), discharged silently.
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

int compute_positive () post<conveyor_ctrl_v>(r: r > 0)
{
  return 1;
}

void use_positive (int x) pre<conveyor_ctrl_v>(x > 0)
{
  (void) x;
}

void caller ()
{
  int r = compute_positive ();
  use_positive (r);
}

int main () { caller (); return 0; }
