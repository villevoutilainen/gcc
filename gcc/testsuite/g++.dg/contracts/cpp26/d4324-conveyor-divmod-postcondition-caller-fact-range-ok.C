// D4324/P2680 item 6, Increment I: a callee's postcondition
// guaranteeing a comparison-based bound ('r > 0', not a bare 'r != 0')
// needs no separate nz-specific handling at all -- it is picked up
// via oa_call_postcondition_range_p feeding oa_get_range, which
// oa_provably_nonzero_p already consults as a supplementary source
// (Increment E1), exactly the same division of labor used everywhere
// else nz-facts and range facts overlap.
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

int g (int x) conveyor post<conveyor_ctrl_v>(r: r > 0)
{
  return x;
}

int f (int x) conveyor
{
  int n = g (x);
  return 10 / n;
}

int main () { return f (5) - 2; }
