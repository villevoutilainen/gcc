// The built-in GIMPLE-pass engine's own conversion-operator lookthrough
// for a *relational* fact (cg_get_relational's own new GIMPLE_CALL
// case, plus cg_check_call's own resolved_other normalization before
// comparing identities) -- both wrap-typed operands are trivially
// copyable (Phase B: decl-keyed, address-taken parameters). g's own
// precondition "x < q" self-trust-seeds a decl-keyed relational fact;
// calling check_relation (x, q) converts both to int for a plain-int
// callee's own precondition "a < b" -- discharged by matching identity
// through both conversion-operator calls, never resolving either
// wrap's value. See ~/gimple-contract-analysis.md.
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
inline constexpr conveyor_ctrl ctrl_v{};

struct wrap {
  int v;
  constexpr wrap (int v_) : v (v_) {}
  constexpr operator int () const conveyor { return v; }
};

int check_relation (int a, int b) pre<ctrl_v> (a < b) { return a; }

int g (wrap x, wrap q) pre<ctrl_v> (x < q)
{
  return check_relation (x, q);
}

int main () { return g (wrap (2), wrap (5)) - 2; } // { dg-warning "cannot verify" }
