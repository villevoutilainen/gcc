// D4324: floating-point analogue of d4324-conveyor-proof-range-mult-
// partial-ok.C -- oa_float_range_multiply/oa_float_range_divide's own
// identical correction for a partially (one-sided) bounded operand (mult)
// or dividend (div), plus oa_get_float_range's own PLUS_EXPR/MINUS_EXPR
// composition, which had the identical bug (each bound previously
// required ALL of both operands' own bounds, unlike the integer PLUS_EXPR/
// MINUS_EXPR case, which already derived each bound independently).  Found
// via the Number-class godbolt demo (godbolt.org/z/PPe8nsb5E) -- see
// d4324-proven-conveyor-number-class-ok.C for the higher-level integration
// test this unit-level test complements.
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

// PLUS_EXPR, one operand only lower-bounded: x >= 1.5 (no upper bound),
// literal 1.0 -> y = x + 1.0 >= 2.5, no upper bound provable.
double
plus_partial (double x)
{
  if (x >= 1.5)
    {
      double y = x + 1.0;
      contract_assert<conveyor_ctrl_v>(y >= 2.5);
    }
  return 0;
}

// MINUS_EXPR, one operand only upper-bounded on the subtrahend:
// x <= 10.0 (no lower bound), literal 3.0 -> y = x - 3.0 <= 7.0, no lower
// bound provable.
double
minus_partial (double x)
{
  if (x <= 10.0)
    {
      double y = x - 3.0;
      contract_assert<conveyor_ctrl_v>(y <= 7.0);
    }
  return 0;
}

// nonneg (lower-bound only) * nonneg (lower-bound only): x >= 3.0,
// q >= 5.0, neither has an upper bound -> y = x*q >= 15.0.
double
mult_nonneg_nonneg_partial (double x, double q)
{
  if (x >= 3.0 && q >= 5.0)
    {
      double y = x * q;
      contract_assert<conveyor_ctrl_v>(y >= 15.0);
    }
  return 0;
}

// nonneg (lower-bound only) * nonpos (upper-bound only): x >= 3.0,
// q <= -5.0 -> y = x*q <= -15.0, no lower bound provable.
double
mult_nonneg_nonpos_partial (double x, double q)
{
  if (x >= 3.0 && q <= -5.0)
    {
      double y = x * q;
      contract_assert<conveyor_ctrl_v>(y <= -15.0);
    }
  return 0;
}

// division, partially-bounded (lower-only) dividend, fully-bounded
// positive divisor: x >= 10.0 (no upper bound), divisor is the constant
// 5.0 -> y = x/5.0 >= 2.0.
double
div_nonneg_dividend_pos_divisor (double x)
{
  if (x >= 10.0)
    {
      double y = x / 5.0;
      contract_assert<conveyor_ctrl_v>(y >= 2.0);
    }
  return 0;
}

// The exact shape from the Number-class demo: percentage >= 0.0 (no
// upper bound) divided by the positive constant 100.0, then added to
// 1.0 -- confirms the PLUS_EXPR fix and the division fix compose
// correctly together, matching increase_by's own factor computation.
double
number_factor_partial (double percentage)
{
  if (percentage >= 0.0)
    {
      double factor = 1.0 + percentage / 100.0;
      contract_assert<conveyor_ctrl_v>(factor >= 1.0);
    }
  return 0;
}

int
main ()
{
  plus_partial (1.5);
  minus_partial (10.0);
  mult_nonneg_nonneg_partial (3.0, 5.0);
  mult_nonneg_nonpos_partial (3.0, -5.0);
  div_nonneg_dividend_pos_divisor (10.0);
  number_factor_partial (100.0);
  return 0;
}
