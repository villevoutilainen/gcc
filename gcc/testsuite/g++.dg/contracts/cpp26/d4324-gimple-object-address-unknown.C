// The built-in GIMPLE-pass engine (-fcontract-conveyor-proofs-gimple)
// deliberately never attempts IILE recursion (see gcc/cp/
// contracts-gimple.cc's own top comment and ~/gimple-contract-analysis.md),
// unlike the mandatory AST-level check, which does resolve this shape
// (see d4324-object-address-iile-ok.C) and so raises no error of its
// own. An honest, documented divergence (an extra warning, never a
// missed violation), exactly mirroring the validated testsuite plugin
// prototype's own gimple-object-address-unknown.C.
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple" }
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
  return deref ([&]{ return &x; }()); // { dg-warning "cannot verify .is_object_address." }
}

int main () { return f () - 5; }
