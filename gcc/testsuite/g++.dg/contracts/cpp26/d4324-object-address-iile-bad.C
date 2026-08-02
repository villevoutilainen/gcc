// D4324/P2680: recursing into an immediately-invoked closure (item 5)
// must still fail to prove when the closure's return value is itself
// unprovable -- here, the closure captures and returns an unrelated
// parameter 'q' by reference, whose provenance is unknown.
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

int g (int* q)
{
  int* p = [&]() { return q; }();
  contract_assert<conveyor_ctrl_v>(std::is_object_address(p)); // { dg-error "cannot prove .is_object_address. for .p." }
  return *p;
}

int main () { int x = 1; return g (&x) - 1; }
