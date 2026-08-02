// D4324/P2680 item 8, Increment E4: a non-ignored, conveyor
// precondition's comparison-shaped conjuncts ('i >= 1 && i <= 100')
// are trusted and seeded as a range fact for the rest of the function
// body, reusing oa_refine_single_comparison directly -- "trusted true"
// for a precondition conjunct is exactly a then-branch refinement.
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

int f (int i) conveyor pre<conveyor_ctrl_v>(i >= 1 && i <= 100)
{
  return 10 / i;
}

int main () { return f (5) - 2; }
