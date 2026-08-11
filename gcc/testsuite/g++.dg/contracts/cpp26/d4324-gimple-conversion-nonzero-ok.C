// The built-in GIMPLE-pass engine's own conversion-operator lookthrough
// for a nonzero-ness fact (cg_provable_nonzero_p's own new GIMPLE_CALL
// case), Phase B (trivially-copyable, decl-keyed). f's own precondition
// "m != 0" self-trust-seeds a decl-keyed nonzero fact for m; dividing
// by divide_by (a, m)'s own substituted, converted argument is then
// provable without ever resolving wrap's own value. See ~/gimple-
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
  constexpr operator int () const conveyor { return v; }
};

int divide_by (int a, int b) pre<ctrl_v> (b != 0) { return a / b; }

int f (int a, wrap m) pre<ctrl_v> (m != 0)
{
  return divide_by (a, m);
}

int main () { return f (10, wrap (2)) - 5; } // { dg-warning "cannot verify" }
