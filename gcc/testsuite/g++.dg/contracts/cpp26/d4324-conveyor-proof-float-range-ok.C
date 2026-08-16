// D4324: floating-point range tracking -- oa_get_float_range's own
// composition, structurally parallel to oa_get_range but via
// REAL_VALUE_TYPE/real.h primitives (see oa_float_range_fact's own
// comment, near oa_range_fact's definition, for why this is a separate
// map/function pair rather than a generalization of the integer path).
// Covers literal comparisons and +, -, *, / composition (both signs,
// decl-vs-decl), and self-referential compound assignment.
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

// Plain literal comparisons against an if-refined range.
double
literal_bounds (double x)
{
  if (x >= 3.0 && x <= 4.0)
    {
      contract_assert<conveyor_ctrl_v>(x >= 3.0 && x <= 4.0);
      contract_assert<conveyor_ctrl_v>(x >= 0.0);
    }
  return x;
}

double
demo_add (double x)
{
  if (x >= 1.5 && x <= 2.5)
    {
      double y = x + 1.0;
      contract_assert<conveyor_ctrl_v>(y >= 2.5 && y <= 3.5);
    }
  return 0;
}

double
demo_sub (double x)
{
  if (x >= 5.0 && x <= 10.0)
    {
      double y = x - 3.0;
      contract_assert<conveyor_ctrl_v>(y >= 2.0 && y <= 7.0);
    }
  return 0;
}

// decl * literal, positive and negative multiplier -- corner-product
// selection needs no special casing for sign, same as the integer path.
double
demo_mult_pos (double x)
{
  if (x >= 2.0 && x <= 4.0)
    {
      double y = x * 3.0;
      contract_assert<conveyor_ctrl_v>(y >= 6.0 && y <= 12.0);
    }
  return 0;
}

double
demo_mult_neg (double x)
{
  if (x >= 2.0 && x <= 9.0)
    {
      double y = x * -2.0;
      contract_assert<conveyor_ctrl_v>(y >= -18.0 && y <= -4.0);
    }
  return 0;
}

// decl * decl, two independently tracked ranges.
double
demo_decl_times_decl (double x, double q)
{
  if (x >= 2.0 && x <= 4.0 && q >= 3.0 && q <= 5.0)
    {
      double y = x * q;
      contract_assert<conveyor_ctrl_v>(y >= 6.0 && y <= 20.0);
    }
  return 0;
}

double
demo_div (double x)
{
  if (x >= 2.0 && x <= 4.0)
    {
      double y = x / 2.0;
      contract_assert<conveyor_ctrl_v>(y >= 1.0 && y <= 2.0);
    }
  return 0;
}

// Self-referential compound assignment ('x += k'/'x *= k'/'x /= k')
// desugars to a MODIFY_EXPR whose RHS names the same decl being
// assigned -- the read-then-write ordering that makes this safe for
// the plain integer range map (see project memory on Rule 1) applies
// identically here, confirmed empirically.
double
demo_plus_assign (double x)
{
  if (x >= 3.0 && x <= 4.0)
    {
      x += 5.0;
      contract_assert<conveyor_ctrl_v>(x >= 8.0 && x <= 9.0);
    }
  return 0;
}

double
demo_mult_assign (double x)
{
  if (x >= 2.0 && x <= 4.0)
    {
      x *= 3.0;
      contract_assert<conveyor_ctrl_v>(x >= 6.0 && x <= 12.0);
    }
  return 0;
}

double
demo_div_assign (double x)
{
  if (x >= 9.0 && x <= 21.0)
    {
      x /= 3.0;
      contract_assert<conveyor_ctrl_v>(x >= 3.0 && x <= 7.0);
    }
  return 0;
}

int
main ()
{
  literal_bounds (3.0);
  demo_add (2.0);
  demo_sub (7.0);
  demo_mult_pos (3.0);
  demo_mult_neg (5.0);
  demo_decl_times_decl (3.0, 4.0);
  demo_div (2.0);
  demo_plus_assign (3.0);
  demo_mult_assign (2.0);
  demo_div_assign (9.0);
  return 0;
}
