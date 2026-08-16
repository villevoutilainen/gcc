// D4324: interval multiplication, genuine violation -- x in [3,4] ->
// y = x*3 in [9,12], so 'y <= 8' is not always true (y could be 12) and
// must be caught as a real proof failure, not silently accepted.
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
demo_mult_bad (int x)
{
  if (x >= 3 && x <= 4)
    {
      int y = x * 3;
      contract_assert<conveyor_ctrl_v>(y <= 8); // { dg-error "provably false" }
    }
  return 0;
}

int main () { return 0; }
