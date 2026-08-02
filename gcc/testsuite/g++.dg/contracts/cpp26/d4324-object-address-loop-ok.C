// D4324/P2680: the loop-header merge rule (item 4) -- std::is_object_address(p)
// is provable after a loop where every reassignment of p inside the
// loop body is independently provable (without depending on p's own
// prior value) and the pre-loop value was already provable too (covers
// zero-iteration execution).
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

int f (int n)
{
  int a = 1, b = 2;
  int* p = &a;
  for (int i = 0; i < n; ++i)
  {
    if (i % 2 == 0) p = &a; else p = &b;
  }
  contract_assert<conveyor_ctrl_v>(std::is_object_address(p));
  return *p;
}

int main () { return f (2) - 2; }
