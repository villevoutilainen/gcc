// D4324: oa_float_range_divide declines when the divisor's own tracked
// interval straddles (or touches) zero, mirroring oa_range_divide's
// identical integer restriction -- a zero-crossing divisor makes the
// quotient potentially unbounded (and, for floats, potentially
// +-Inf/NaN at the boundary itself).
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

double
demo_crosses_zero (double x, double q)
{
  if (x >= 10.0 && x <= 20.0 && q >= -2.0 && q <= 4.0)
    {
      double y = x / q;
      contract_assert<conveyor_ctrl_v>(y >= 0.0); // { dg-warning "cannot verify" }
    }
  return 0;
}

int main () { return 0; }
