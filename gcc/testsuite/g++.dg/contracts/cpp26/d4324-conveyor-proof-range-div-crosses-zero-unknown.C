// D4324: oa_range_divide declines when the divisor's own tracked
// interval straddles (or touches) zero -- a zero-crossing divisor makes
// the quotient potentially unbounded, unrepresentable as one [lo,hi],
// so composition must stay "cannot verify" rather than silently
// accepting or (worse) evaluating a division by zero at analysis time.
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

int
demo_crosses_zero (int x, int q)
{
  if (x >= 10 && x <= 20 && q >= -2 && q <= 4)
    {
      int y = x / q;
      contract_assert<conveyor_ctrl_v>(y >= 0); // { dg-warning "cannot verify" }
    }
  return 0;
}

int main () { return 0; }
