// GIMPLE mirror of d4324-reference-ownership-cross-parameter-alias-ok.C
// -- see that file's own comment for the full rationale. Gated by
// -fcontract-conveyor-proofs-gimple, run alongside the ordinary
// mandatory AST-level check -- both independently accept this.
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

int middle (T* p) conveyor
  pre<conveyor_ctrl_v> (std::is_object_address (p))
{
  return use_val_mut (*p);
}

int outer (T& a, T& b) conveyor
{
  middle (&a);
  return b.v;
}

int main ()
{
  T x{1};
  if (outer (x, x) != 5) return 1;
  T y{1}, z{2};
  if (outer (y, z) != 2) return 1;
  return 0;
}
