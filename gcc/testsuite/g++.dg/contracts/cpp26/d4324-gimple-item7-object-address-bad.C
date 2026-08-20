// Companion to d4324-gimple-item7-object-address-ok.C: OA_UNKNOWN-turned-
// error for Q1 -- P's own pointee is not provably an object address, so
// '*p' may not satisfy a conveyor callee's reference parameter. Gated
// by -fcontract-conveyor-proofs-gimple, run alongside the ordinary
// mandatory AST-level check -- both independently reject this.
// { dg-do compile }
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

struct T { int v; };
int use_val_const (const T& x) conveyor { return x.v; }

int q1_bad (T *p) conveyor
{
  return use_val_const (*p); // { dg-error "cannot prove .is_object_address." }
                             // { dg-message "no fact relating this value" "unprovable reason" { target *-*-* } .-1 }
}

int main () { return 0; }
