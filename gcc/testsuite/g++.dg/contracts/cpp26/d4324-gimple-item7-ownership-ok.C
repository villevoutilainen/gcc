// D4324/P2680 item 7's Q2 (ownership/cone-of-evaluation), ported to the
// built-in GIMPLE-pass engine (cg_provably_owned_p) -- previously
// entirely absent from GIMPLE. A reference parameter RECEIVED by this
// function is OWNED: forwarding it non-const to a further conveyor call
// doesn't extend the cone, since the caller already handed it over
// legitimately. Gated by -fcontract-conveyor-proofs-gimple, run
// alongside the ordinary mandatory AST-level check -- both must
// independently accept this.
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

struct T { int v; };
int use_val_mut (T& x) conveyor { x.v = 5; return x.v; }

int accept_ref_param (T& y) conveyor
{
  return use_val_mut (y);
}

int main ()
{
  T t{1};
  return accept_ref_param (t) - 5;
}
