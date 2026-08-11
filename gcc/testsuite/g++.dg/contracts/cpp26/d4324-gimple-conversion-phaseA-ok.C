// The built-in GIMPLE-pass engine's own conversion-operator lookthrough
// (cg_resolve_conversion_receiver in contracts-gimple.cc) for a class
// type with a non-trivial destructor -- the Itanium ABI's "non-trivial
// for the purposes of calls" rule passes such a type by invisible
// reference, so wrap's own parameter already has an ordinary SSA name
// (no address-taking/decl-keyed fallback needed at all, unlike
// d4324-gimple-conversion-phaseB-ok.C's trivially-copyable case). f's
// own precondition "q < 5" self-trust-seeds a range fact for q; that
// fact is consulted when f calls need_small (q), whose own precondition
// requires "x < 10" -- provable without ever resolving wrap's own
// value. See ~/gimple-contract-analysis.md.
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
  operator int () const conveyor { return v; }
};

int need_small (int x) pre<ctrl_v> (x < 10) { return x; }

int f (wrap q) pre<ctrl_v> (q < 5)
{
  return need_small (q);
}

int main () { return f (wrap (2)) - 2; } // { dg-warning "cannot verify" }
