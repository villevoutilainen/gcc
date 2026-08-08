// gimple_object_address_plugin.cc: a deliberately out-of-scope shape for
// this prototype -- deref's own argument is the result of an
// immediately-invoked closure call (an IILE), which the existing
// mandatory AST-level check already recognizes and resolves (item 5's
// own IILE recursion; compare gcc/testsuite/g++.dg/contracts/cpp26/
// d4324-object-address-iile-ok.C), so it accepts this silently. This
// prototype's own provable_object_address_p never attempts IILE
// recursion at all (see the plugin's own top comment) -- the closure's
// own operator() call is just an ordinary, uninterpreted GIMPLE_CALL to
// a callee with no declared postcondition of its own, so item 6's own
// new call_postcondition_guarantees_object_address_p check (added for
// the *previous* unknown scenario this test used to cover -- see the
// git history for gimple-object-address-postcondition-ok.C, which now
// covers that shape instead) finds nothing and this reports "cannot
// verify." An honest, documented divergence (an extra warning, never a
// missed violation), not a soundness bug in either engine. See
// ~/gimple-contract-analysis.md.
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

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

int deref (int* p) pre<conveyor_ctrl_v>(std::is_object_address (p))
{
  return *p;
}

int f ()
{
  int x = 5;
  return deref ([&]{ return &x; }()); // { dg-warning "gimple-oa: cannot verify" }
}

int main () { return f () - 5; }
