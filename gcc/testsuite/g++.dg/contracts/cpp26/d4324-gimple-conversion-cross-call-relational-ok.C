// The built-in GIMPLE-pass engine's own cross-call follow-up to
// d4324-gimple-conversion-relational-ok.C: forwarding two class-typed
// decls BY VALUE to a *different* function needing the same relational
// fact, for a non-trivially-copyable type. g's own precondition
// "x < q" self-trust-seeds an SSA-keyed relational fact between its
// own reference-passed x/q; each is materialized as a real copy-
// constructor call at the 'f (x, q)' call site (the same shape as
// d4324-gimple-conversion-cross-call-simple-ok.C), resolved via
// cg_resolve_call_argument for both SUB_PARAM and SUB_OTHER, so the
// obligation is discharged purely by matching identity, never
// resolving either wrap's value. See ~/gimple-contract-analysis.md.
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
  wrap (int v_) : v (v_) {}
  wrap (const wrap &other) : v (other.v) {}
  ~wrap () {}
  operator int () const { return v; }
};

int f (wrap x, wrap q) pre<ctrl_v> (x < q) { return x; }

int g (wrap x, wrap q) pre<ctrl_v> (x < q)
{
  return f (x, q);
}

int main () { return g (wrap (2), wrap (5)) - 2; } // { dg-warning "cannot verify" }
