// D4324: interval multiplication/division with a PARTIALLY (one-sided)
// bounded operand -- oa_range_multiply/oa_range_divide used to require
// both operands (mult) or the dividend (div) fully two-sided bounded,
// declining entirely otherwise, even when an operand's own SIGN was still
// fully determinable from just one known bound. Correction: derive
// whichever single result bound is soundly computable from the known
// corner(s), for each of the four sign combinations, rather than
// declining outright. Found via the Number-class godbolt demo
// (godbolt.org/z/PPe8nsb5E) -- see d4324-proven-conveyor-number-class-
// ok.C for the higher-level integration test this unit-level test
// complements. The divisor (unlike the dividend) must still be fully
// two-sided and one-signed -- unaffected by this correction.
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

// nonneg (lower-bound only) * nonneg (lower-bound only): x >= 3, q >= 5,
// neither has an upper bound -> y = x*q >= 15, no upper bound provable.
int
mult_nonneg_nonneg_partial (int x, int q)
{
  if (x >= 3 && q >= 5)
    {
      int y = x * q;
      contract_assert<conveyor_ctrl_v>(y >= 15);
    }
  return 0;
}

// nonpos (upper-bound only) * nonpos (upper-bound only): x <= -3, q <= -5
// -> y = x*q >= 15 (product of two non-positives is non-negative,
// minimized at the two magnitudes closest to zero), no upper bound.
int
mult_nonpos_nonpos_partial (int x, int q)
{
  if (x <= -3 && q <= -5)
    {
      int y = x * q;
      contract_assert<conveyor_ctrl_v>(y >= 15);
    }
  return 0;
}

// nonneg (lower-bound only) * nonpos (upper-bound only): x >= 3, q <= -5
// -> y = x*q <= -15 (mixed-sign product is non-positive, maximized --
// closest to zero -- at the two magnitudes closest to zero), no lower
// bound.
int
mult_nonneg_nonpos_partial (int x, int q)
{
  if (x >= 3 && q <= -5)
    {
      int y = x * q;
      contract_assert<conveyor_ctrl_v>(y <= -15);
    }
  return 0;
}

// nonpos (upper-bound only) * nonneg (lower-bound only): mirror of the
// case just above, operands swapped.
int
mult_nonpos_nonneg_partial (int x, int q)
{
  if (x <= -3 && q >= 5)
    {
      int y = x * q;
      contract_assert<conveyor_ctrl_v>(y <= -15);
    }
  return 0;
}

// division, partially-bounded dividend, fully-bounded positive divisor:
// x >= 10 (no upper bound), divisor is the constant 5 -> y = x/5 >= 2.
int
div_nonneg_dividend_pos_divisor (int x)
{
  if (x >= 10)
    {
      int y = x / 5;
      contract_assert<conveyor_ctrl_v>(y >= 2);
    }
  return 0;
}

// division, partially-bounded dividend, fully-bounded negative divisor:
// x >= 10 (no upper bound), divisor is the constant -5 -> y = x/-5 <= -2.
int
div_nonneg_dividend_neg_divisor (int x)
{
  if (x >= 10)
    {
      int y = x / -5;
      contract_assert<conveyor_ctrl_v>(y <= -2);
    }
  return 0;
}

// division, partially-bounded (upper-only) dividend, fully-bounded
// negative divisor: x <= -10 (no lower bound), divisor is the constant
// -5 -> y = x/-5 >= 2.
int
div_nonpos_dividend_neg_divisor (int x)
{
  if (x <= -10)
    {
      int y = x / -5;
      contract_assert<conveyor_ctrl_v>(y >= 2);
    }
  return 0;
}

int
main ()
{
  mult_nonneg_nonneg_partial (3, 5);
  mult_nonpos_nonpos_partial (-3, -5);
  mult_nonneg_nonpos_partial (3, -5);
  mult_nonpos_nonneg_partial (-3, 5);
  div_nonneg_dividend_pos_divisor (10);
  div_nonneg_dividend_neg_divisor (10);
  div_nonpos_dividend_neg_divisor (-10);
  return 0;
}
