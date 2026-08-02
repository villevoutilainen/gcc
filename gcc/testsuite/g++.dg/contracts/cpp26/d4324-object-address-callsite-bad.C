// D4324/P2680: the call-site precondition-obligation mechanism (item 7)
// must fail when the caller's argument expression isn't provable in the
// caller's own context -- confirming the error is reported at the call
// site (not deref's own definition), since deref's own precondition is
// only ever trusted, never re-derived, inside its own body.
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

int deref (int* p) pre<conveyor_ctrl_v>(std::is_object_address(p))
{
  return *p;
}

int caller (int* q)
{
  return deref (q); // { dg-error "cannot prove .is_object_address. for .q." }
}

int main () { int x = 1; return caller (&x) - 1; }
