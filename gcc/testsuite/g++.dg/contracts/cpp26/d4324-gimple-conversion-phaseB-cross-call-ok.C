// A pleasant surprise found while locking in d4324-gimple-conversion-
// cross-call-unknown.C's own boundary: unlike a Phase A (reference-
// passed) type, forwarding a Phase B (trivially-copyable, decl-keyed)
// class-typed decl BY VALUE to a *different* function needing the same
// fact already works, with no extra code -- verified via -fdump-tree-
// ssa that 'f (q)' reuses q's own PARM_DECL directly as the call
// argument (no GIMPLE-level copy-constructor call at all, since a
// trivially-copyable by-value argument needs no constructor), so
// cg_check_call's own gimple_call_arg lookup at that call site already
// sees q itself, resolved by the plain decl-keyed lookup with no
// separate "trace back through a copy" step needed. See ~/gimple-
// contract-analysis.md.
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
  constexpr operator int () const { return v; }
};

int f (wrap x) pre<ctrl_v> (x < 5) { return x; }

int g (wrap q) pre<ctrl_v> (q < 5)
{
  return f (q);
}

int main () { return g (wrap (2)) - 2; } // { dg-warning "cannot verify" }
