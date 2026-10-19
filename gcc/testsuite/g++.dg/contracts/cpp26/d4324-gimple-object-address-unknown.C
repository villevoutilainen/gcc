// The built-in GIMPLE-pass engine (-fcontract-conveyor-proofs-gimple)
// proves the result of an immediately invoked lambda returning a
// capture's address, as the AST engine does (d4324-object-address-iile-
// ok.C): it used to warn here, a documented divergence, no longer.
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
  return deref ([&]{ return &x; }());
}

int main () { return f () - 5; }
