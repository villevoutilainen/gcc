// D4324/P2680: -fcontract-conveyor-proofs, comparison-conjunct proof,
// OA_UNKNOWN case -- an ordinary, uncontracted parameter with no
// established range fact at all.  A weaker signal than a proven
// violation: a warning, not an error, and compilation still succeeds.
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

void use_positive (int x) pre<conveyor_ctrl_v>(x > 0)
{
  (void) x;
}

void caller (int untrusted)
{
  use_positive (untrusted); // { dg-warning "cannot verify" }
}

int main () { caller (1); return 0; }
