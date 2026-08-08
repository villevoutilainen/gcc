// The built-in GIMPLE-pass engine's own self-trust case for a
// relational precondition (-fcontract-conveyor-proofs-gimple): g's own
// declared precondition "x < q" is trusted unconditionally for the
// rest of g's own body (cg_seed_self_trust's new relational loop,
// keyed on ssa_default_def), so forwarding both parameters unchanged
// to f's identically-shaped precondition is discharged purely by
// matching established SSA names -- never resolving either parameter
// to any numeric value.
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

int f (int x, int const q) pre<ctrl_v> (x < q) post<ctrl_v> (r: r < q) { return x; }
int g (int x, int const q) pre<ctrl_v> (x < q) { return f (x, q); }

int main () { return g (2, 5) - 2; }
