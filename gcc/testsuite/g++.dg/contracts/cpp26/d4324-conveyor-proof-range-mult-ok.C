// D4324: interval multiplication -- oa_get_range's own MULT_EXPR
// composition (via the shared oa_range_multiply corner-product helper,
// also reused by oa_scan_overflow_in_expr's pre-existing overflow-
// safety check). Standard interval multiplication needs no special-
// casing for sign: the four corner products (lo*lo, lo*hi, hi*lo,
// hi*hi) and their own min/max already produce the correct result
// regardless of which operand is negative, or whether both operands
// are independently-tracked decls rather than one being a literal.
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

// decl * literal, positive multiplier: x in [3,4] -> y = x*3 in [9,12]
int
positive_multiplier (int x)
{
  if (x >= 3 && x <= 4)
    {
      int y = x * 3;
      contract_assert<conveyor_ctrl_v>(y >= 9 && y <= 12);
    }
  return 0;
}

// decl * literal, negative multiplier -- confirms the corner-product
// approach correctly flips which side becomes lo/hi with no special
// casing: x in [2,9] -> y = x*-2 in [-18,-4]
int
negative_multiplier (int x)
{
  if (x >= 2 && x <= 9)
    {
      int y = x * -2;
      contract_assert<conveyor_ctrl_v>(y >= -18 && y <= -4);
    }
  return 0;
}

// decl * decl, two independently tracked ranges (not one side a
// literal): x in [3,4], q in [5,6] -> y = x*q in [15,24]
int
decl_times_decl (int x, int q)
{
  if (x >= 3 && x <= 4 && q >= 5 && q <= 6)
    {
      int y = x * q;
      contract_assert<conveyor_ctrl_v>(y >= 15 && y <= 24);
    }
  return 0;
}

int
main ()
{
  positive_multiplier (3);
  negative_multiplier (5);
  decl_times_decl (3, 5);
  return 0;
}
