// D4324/P2680: a postcondition's std::is_object_address(r) must fail to
// prove when the returned value is an unrelated, unprovable parameter --
// confirms the postcondition-proof machinery actually distinguishes a
// provable return value (see d4324-object-address-postcondition-ok.C)
// from an unprovable one, rather than accepting both.
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

int* f (int* q) post<conveyor_ctrl_v>(r: std::is_object_address(r)) // { dg-error "cannot prove .is_object_address. for .r." }
{
  return q;
}

int main () { int x = 1; return *f (&x) - 1; }
