// D4324/P2680: the loop-header merge rule (item 4) must reject the case
// where every in-loop reassignment is independently provable but the
// pre-loop value was not -- required because the loop might execute
// zero times, in which case the unprovable pre-loop value would reach
// the use after the loop unchanged.
// { dg-do compile { target c++26 } }
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

int a = 1;

int f (int n, int* q)
{
  int* p = q; // unprovable pre-loop value
  for (int i = 0; i < n; ++i)
    p = &a; // every in-loop reassignment is provable on its own
  contract_assert<conveyor_ctrl_v>(std::is_object_address(p)); // { dg-error "cannot prove .is_object_address. for .p." }
  return *p;
}

int main () { int x = 1; return f (1, &x) - 1; }
