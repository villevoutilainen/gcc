// D4324: interval division -- oa_get_range's own TRUNC_DIV_EXPR
// composition (via the shared oa_range_divide corner-quotient helper).
// Covers decl/literal (both signs) and decl/decl.
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

// decl / literal, positive divisor: x in [10,20] -> y = x/5 in [2,4]
int
demo_pos (int x)
{
  if (x >= 10 && x <= 20)
    {
      int y = x / 5;
      contract_assert<conveyor_ctrl_v>(y >= 2 && y <= 4);
    }
  return 0;
}

// decl / literal, negative divisor -- confirms the sign flip needs no
// special casing: x in [10,20] -> y = x/-5 in [-4,-2]
int
demo_neg (int x)
{
  if (x >= 10 && x <= 20)
    {
      int y = x / -5;
      contract_assert<conveyor_ctrl_v>(y >= -4 && y <= -2);
    }
  return 0;
}

// decl / decl, two independently tracked ranges: x in [10,20], q in
// [2,4] -> corners 10/2=5, 10/4=2, 20/2=10, 20/4=5 -> range [2,10]
int
demo_decl_decl (int x, int q)
{
  if (x >= 10 && x <= 20 && q >= 2 && q <= 4)
    {
      int y = x / q;
      contract_assert<conveyor_ctrl_v>(y >= 2 && y <= 10);
    }
  return 0;
}

int
main ()
{
  demo_pos (10);
  demo_neg (10);
  demo_decl_decl (10, 2);
  return 0;
}
