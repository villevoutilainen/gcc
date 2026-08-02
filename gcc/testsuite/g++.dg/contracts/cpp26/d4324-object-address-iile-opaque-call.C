// D4324/P2680: an ordinary (non-immediately-invoked) function call must
// never be interpreted the way an IILE is -- confirms the walker
// correctly leaves provenance running through an opaque call
// conservatively unprovable, rather than trying to inline/analyze an
// arbitrary callee's body.
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
int* helper () { return &a; }

int g ()
{
  int* p = helper ();
  contract_assert<conveyor_ctrl_v>(std::is_object_address(p)); // { dg-error "cannot prove .is_object_address. for .p." }
  return *p;
}

int main () { return g () - 1; }
