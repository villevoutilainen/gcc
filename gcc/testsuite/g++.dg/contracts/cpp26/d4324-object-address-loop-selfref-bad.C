// D4324/P2680: the loop-header merge rule (item 4) must reject a
// reassignment whose RHS circularly depends on the variable's own prior
// value (here, 'p = p + 1;') -- this is exactly the case the rule's
// "no self-reference" restriction exists to catch, since without it a
// one-pass (non-fixpoint) analysis could not otherwise distinguish this
// from a legitimately provable reassignment.
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

int f (int n, int* q)
{
  int a = 1;
  int* p = &a;
  for (int i = 0; i < n; ++i)
    p = p + 1;
  contract_assert<conveyor_ctrl_v>(std::is_object_address(p)); // { dg-error "cannot prove .is_object_address. for .p." }
  return *p;
}

int main () { int x = 1; return f (1, &x) - 1; }
