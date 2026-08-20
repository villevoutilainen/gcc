// D4324/P2680: std::is_object_address(p) on a bare parameter, checked
// from inside the function that receives it, is NOT provable -- the
// callee has no visibility into how the caller derived the argument
// (this is exactly why a precondition's is_object_address must instead
// be proven at each call site; see d4324-object-address-outside-conveyor.C
// and the plan's item 7 for the not-yet-implemented call-site
// mechanism). Confirms the walker correctly fails closed rather than
// guessing.
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

int f (int* p)
{
  contract_assert<conveyor_ctrl_v>(std::is_object_address(p)); // { dg-error "cannot prove .is_object_address. for .p." }
                                                                // { dg-message "no fact relating this value" "unprovable reason" { target *-*-* } .-1 }
  return *p;
}

int main () { int x = 1; return f (&x) - 1; }
