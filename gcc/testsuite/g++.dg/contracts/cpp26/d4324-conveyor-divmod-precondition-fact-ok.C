// D4324/P2680 item 8, Increment E-divmod: a non-ignored, conveyor
// precondition's 'b != 0' conjunct is trusted and seeded as a
// "provably nonzero" fact for the rest of the function body, the same
// way an is_object_address(E) conjunct seeds the pointer map.
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

int f (int b) conveyor pre<conveyor_ctrl_v>(b != 0)
{
  return 10 / b;
}

int main () { return f (5) - 2; }
