// The built-in GIMPLE-pass engine's own conversion-operator lookthrough
// for a trivially-copyable class type (only inline/defaulted special
// members) -- passed by value in a register, so calling its own
// conversion operator requires taking its address, which makes the
// parameter TREE_ADDRESSABLE and therefore never promoted to SSA form
// at all. cg_decl_safe_for_conversion_tracking_p proves q is never
// written to anywhere in f's own body (the only address-of-q uses are
// the conversion-operator call itself and this precondition's own
// runtime-check capture struct, both recognized as safe), so self-trust
// still seeds a decl-keyed range fact for q from f's own precondition
// "q < 5", consulted the same way as d4324-gimple-conversion-phaseA-
// ok.C's SSA-keyed one when f calls need_small (q). See ~/gimple-
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

int need_small (int x) pre<ctrl_v> (x < 10) { return x; }

int f (wrap q) pre<ctrl_v> (q < 5)
{
  return need_small (q);
}

int main () { return f (wrap (2)) - 2; } // { dg-warning "cannot verify" }
