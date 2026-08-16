// D4324: directed-rounding soundness -- 0.1 + 0.2 famously does not
// round-trip to the double literal 0.3 in IEEE 754 (the exact sum
// rounds up past it), so a composed range straddling that boundary
// must stay "cannot verify" in both directions rather than either
// naively proving a bound that happens to be wrong, or refusing to
// prove the bound that genuinely does hold. Each check isolated in its
// own function -- an unproven contract_assert still gets established
// as a trusted axiom for later code in the *same* function, so multiple
// checks about the same variable in one function would contaminate
// each other rather than exercising the composition itself in
// isolation.
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

// x in [0.1,0.2] -> y = x + 0.1: the lower corner (0.1+0.1) is exact
// (doubling is always exact, barring overflow), so y >= 0.2 is
// genuinely, soundly provable.
double
demo_ge (double x)
{
  if (x >= 0.1 && x <= 0.2)
    {
      double y = x + 0.1;
      contract_assert<conveyor_ctrl_v>(y >= 0.2);
    }
  return 0;
}

// The upper corner (0.2+0.1) rounds to just above the double literal
// 0.3 -- 'y <= 0.3' must stay unprovable (not wrongly accepted).
double
demo_le (double x)
{
  if (x >= 0.1 && x <= 0.2)
    {
      double y = x + 0.1;
      contract_assert<conveyor_ctrl_v>(y <= 0.3); // { dg-warning "cannot verify" }
    }
  return 0;
}

// Symmetric: 'y > 0.3' isn't provable either -- y's own lower corner
// (exactly 0.2) doesn't satisfy it, even though the upper corner does.
double
demo_gt (double x)
{
  if (x >= 0.1 && x <= 0.2)
    {
      double y = x + 0.1;
      contract_assert<conveyor_ctrl_v>(y > 0.3); // { dg-warning "cannot verify" }
    }
  return 0;
}

// 'y < 0.2' is disjoint from y's own established lower bound (exactly
// 0.2) -- genuinely, provably false.
double
demo_lt (double x)
{
  if (x >= 0.1 && x <= 0.2)
    {
      double y = x + 0.1;
      contract_assert<conveyor_ctrl_v>(y < 0.2); // { dg-error "provably false" }
    }
  return 0;
}

int main () { return 0; }
