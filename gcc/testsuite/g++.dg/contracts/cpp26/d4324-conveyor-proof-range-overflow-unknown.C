// D4324: each composed bound (lo and hi alike) must be checked against
// BOTH type_min and type_max, not just its own "natural" one-sided
// threshold -- an overflowing computation's exact lo can land on the
// wrong side of type_max just as easily as its hi can (a huge positive
// lo is trivially ">= type_min" while still being just as
// unrepresentable), so checking lo only against type_min (and hi only
// against type_max) would let an overflowed value through undetected.
// x tracked near INT_MAX/INT_MIN so 'x * 2' provably overflows/
// underflows for every value in range -- composition must decline
// entirely (neither direction silently "proven").
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
demo_overflow (int x)
{
  if (x >= __INT_MAX__ - 5 && x <= __INT_MAX__)
    {
      int y = x * 2;
      contract_assert<conveyor_ctrl_v>(y > 0); // { dg-warning "cannot verify" }
    }
  return 0;
}

int
demo_underflow (int x)
{
  if (x <= -__INT_MAX__ + 5 && x >= -__INT_MAX__ - 1)
    {
      int y = x * 2;
      contract_assert<conveyor_ctrl_v>(y < 0); // { dg-warning "cannot verify" }
    }
  return 0;
}

int main () { return 0; }
