// D4324/P2680: reading a by-reference lambda-capture proxy's *value*
// directly (as opposed to '&captured', already covered by
// d4324-object-address-iile-ok.C) must also correctly redirect through
// the capture-proxy mechanism. Found and fixed alongside Increment
// E-divmod: this arrives as INDIRECT_REF(proxy) at this stage, not the
// bare proxy VAR_DECL the existing capture-proxy check expected --
// previously fell through to "unprovable" unconditionally regardless
// of whether the captured pointer actually was.
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

int g ()
{
  int a = 1;
  int* p = &a;
  int* q = [&]() { return p; }();
  contract_assert<conveyor_ctrl_v>(std::is_object_address(q));
  return *q;
}

int main () { return g () - 1; }
