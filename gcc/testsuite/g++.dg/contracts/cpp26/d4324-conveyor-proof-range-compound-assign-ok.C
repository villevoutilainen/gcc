// D4324: self-referential compound assignment ('x += k', 'x -= k',
// 'x *= k', 'x /= k') desugars to a MODIFY_EXPR whose RHS names the
// same decl being assigned -- confirms the generalized interval
// composition handles this self-referential shape correctly (the
// established range is read before being overwritten, so no ordering
// hazard like the one already fixed for relational facts applies here).
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

// x in [3,4], then x += 5 -> [8,9]
int
demo_plus_assign (int x)
{
  if (x >= 3 && x <= 4)
    {
      x += 5;
      contract_assert<conveyor_ctrl_v>(x >= 8 && x <= 9);
    }
  return 0;
}

// x in [10,20], then x -= 5 -> [5,15]
int
demo_minus_assign (int x)
{
  if (x >= 10 && x <= 20)
    {
      x -= 5;
      contract_assert<conveyor_ctrl_v>(x >= 5 && x <= 15);
    }
  return 0;
}

// x in [2,4], then x *= 3 -> [6,12]
int
demo_mult_assign (int x)
{
  if (x >= 2 && x <= 4)
    {
      x *= 3;
      contract_assert<conveyor_ctrl_v>(x >= 6 && x <= 12);
    }
  return 0;
}

// x in [9,21], then x /= 3 -> [3,7]
int
demo_div_assign (int x)
{
  if (x >= 9 && x <= 21)
    {
      x /= 3;
      contract_assert<conveyor_ctrl_v>(x >= 3 && x <= 7);
    }
  return 0;
}

int
main ()
{
  demo_plus_assign (3);
  demo_minus_assign (10);
  demo_mult_assign (2);
  demo_div_assign (9);
  return 0;
}
