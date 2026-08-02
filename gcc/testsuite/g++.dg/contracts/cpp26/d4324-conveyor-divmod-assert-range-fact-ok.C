// D4324/P2680 item 8, Increment E4: a preceding, conveyor, non-ignored
// contract_assert's own comparison conjunct ('i > 0') establishes a
// usable range fact for later code, the same escape hatch already
// used for is_object_address/nonzero-ness fact-sourcing.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

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

int f (int i) conveyor
{
  contract_assert<conveyor_ctrl_v>(i > 0);
  return 10 / i;
}

int main () { return f (5) - 2; }
