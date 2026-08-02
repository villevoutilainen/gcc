// D4324/P2680: the if/else merge rule requires BOTH arms to
// independently prove is_object_address -- one arm assigning an
// unprovable value (a plain, unrelated parameter here) must fail the
// merge, even though the other arm alone would have been fine.
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

int f (int x, int* q)
{
  int a = 1;
  int* p;
  if (x > 0)
    p = &a;
  else
    p = q; // unprovable on this arm
  contract_assert<conveyor_ctrl_v>(std::is_object_address(p)); // { dg-error "cannot prove .is_object_address. for .p." }
  return *p;
}

int main () { int y = 1; return f (1, &y) - 1; }
