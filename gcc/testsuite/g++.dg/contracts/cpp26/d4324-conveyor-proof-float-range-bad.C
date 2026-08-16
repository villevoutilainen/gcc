// D4324: floating-point range tracking, genuine violation -- x in
// [3.0,4.0] refined via the if-guard, then a disjoint literal bound
// must be caught, not silently accepted.
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
demo_literal_bad (double x)
{
  if (x >= 3.0 && x <= 4.0)
    {
      contract_assert<conveyor_ctrl_v>(x < 2.0); // { dg-error "provably false" }
    }
  return x;
}

int main () { return 0; }
