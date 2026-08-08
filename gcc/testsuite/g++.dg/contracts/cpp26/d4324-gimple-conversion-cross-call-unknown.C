// The built-in GIMPLE-pass engine's own conversion-operator lookthrough
// is deliberately scoped to same-function self-trust + consult only
// (d4324-gimple-conversion-phaseA/phaseB-ok.C). Forwarding a class-typed
// decl BY VALUE to a *different* function that needs the same fact is
// explicitly out of scope -- but only for a Phase A (reference-passed)
// type: verified via -fdump-tree-ssa that forwarding a Phase B
// (trivially-copyable) decl reuses the SAME PARM_DECL directly as the
// call argument (no GIMPLE-level copy statement at all, since a
// trivially-copyable by-value argument needs no constructor call), so
// that case actually already works via the plain decl-keyed lookup.
// wrap here has a non-trivial destructor, so g's own forwarding call
// 'f (q)' instead materializes a fresh copy via a real constructor call
// ('wrap::wrap (&D.NNNN, q)'), and cg_check_call's own gimple_call_arg
// lookup at that call site sees only that temporary, not q itself --
// resolving it would need its own "trace back through a copy-
// constructor call" step (a GIMPLE-level analogue of the TARGET_EXPR
// handling contracts.cc's own AST-walk engine already has), not
// implemented here. This locks in that boundary so it isn't later
// mistaken for a regression. See ~/gimple-contract-analysis.md.
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

int f (wrap x) pre<ctrl_v> (x < 5) { return x; }

int g (wrap q) pre<ctrl_v> (q < 5)
{
  return f (q); // { dg-warning "cannot verify" }
}

int main () { return g (wrap (2)) - 2; } // { dg-warning "cannot verify" }
