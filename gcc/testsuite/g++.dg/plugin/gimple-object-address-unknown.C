// gimple_object_address_plugin.cc: deref's argument is the result of an
// immediately-invoked closure.  The plugin's object-address facts don't
// look into a closure's body (the built-in engines do, compare
// gcc/testsuite/g++.dg/contracts/cpp26/d4324-object-address-iile-ok.C):
// the closure's operator() is an ordinary call whose callee declares no
// postcondition, so the symbolic obligation is reported as unverified --
// an extra warning, never a missed violation.
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple -fcontract-analysis-proofs=plugin -fcontract-symbolic-proofs" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
namespace sc = std::contracts;

struct symbolic_ctrl {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

int deref (int* p) pre<symbolic_ctrl_v>(std::is_object_address (p))
{
  return *p;
}

int f ()
{
  int x = 5;
  return deref ([&]{ return &x; }()); // { dg-warning "cannot verify" }
}

int main () { return f () - 5; }
