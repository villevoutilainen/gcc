// D4324/P2680 item 8's overflow scan: general binary MULT_EXPR needs a
// fully two-sided range on *both* operands -- a is fully bounded, but b
// only has a lower bound (no upper), so the corner-product computation
// can't run and 'a * b' is correctly rejected as unprovable.
// { dg-do compile { target c++26 } }
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

int f (int a, int b) conveyor
  pre<conveyor_ctrl_v>(a > 0 && a < 100 && b > 0)
{
  return a * b; // { dg-error "not provably free of overflow in a conveyor function" }
}

int main () { return f (2, 3); }
