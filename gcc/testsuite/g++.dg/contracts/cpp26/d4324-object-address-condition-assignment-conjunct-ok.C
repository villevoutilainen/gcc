// D4324/P2680, Increment K: a side benefit of processing the
// condition per-conjunct -- oa_track_condition_assignment (previously
// called once on the whole condition, so it never unwrapped a
// top-level '&&' at all) is now called once per conjunct, correctly
// tracking an assignment nested in a middle conjunct of a '&&'-chain
// ('a && (p = &x) != nullptr && b'), which was previously unreachable
// by this mechanism entirely.
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

int x;

int f (bool a, bool b)
{
  int* p = nullptr;
  if (a && (p = &x) != nullptr && b)
    {
      contract_assert<conveyor_ctrl_v>(std::is_object_address (p));
      return *p;
    }
  return 0;
}

int main () { x = 5; return f (true, true) - 5; }
