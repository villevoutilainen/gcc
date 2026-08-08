// The built-in GIMPLE-pass engine's own cross-call follow-up to
// d4324-gimple-conversion-phaseA-ok.C: forwarding a class-typed decl BY
// VALUE to a *different* function needing the same range fact, for a
// non-trivially-copyable type (a user-provided copy ctor and
// destructor). Confirmed via -fdump-tree-ssa that Phase B (trivially-
// copyable) forwarding already worked with no changes at all
// (d4324-gimple-conversion-phaseB-cross-call-ok.C) -- this is
// specifically the Phase A case, where 'f (q)' instead materializes a
// fresh copy via a real constructor call ('wrap::wrap (&D.NNNN, q)'),
// confirmed to remain in the SAME basic block as the forwarding call
// even though a non-trivial destructor's own try/finally region is
// visible at the GENERIC level. cg_resolve_copy_construction_receiver
// recognizes this shape (a copy/move constructor called with its own
// *last* argument being the real source -- an extra, compiler-internal
// leading argument was found by direct testing to sometimes precede
// it, mirroring contracts.cc's own identical finding for AGGR_INIT_EXPR)
// and resolves through to q itself via cg_resolve_call_argument, now
// used throughout cg_check_call in place of bare gimple_call_arg. See
// ~/gimple-contract-analysis.md.
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
  return f (q);
}

int main () { return g (wrap (2)) - 2; } // { dg-warning "cannot verify" }
