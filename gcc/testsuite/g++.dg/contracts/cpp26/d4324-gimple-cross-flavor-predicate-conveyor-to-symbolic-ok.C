// The built-in GIMPLE-pass engine's own one-way trust between the two
// control-object flavors, for named predicates (cg_pred_fact's own
// CONVEYOR_ESTABLISHED) -- see d4324-gimple-cross-flavor-predicate-
// symbolic-to-conveyor-unknown.C for the forbidden direction.
// open_conveyor()'s CONVEYOR-flavored postcondition establishes
// is_opened(this); read_symbolic()'s SYMBOLIC-flavored precondition
// requires the same fact on the same object -- a conveyor-established
// fact is trustworthy enough for symbolic's own check to rely on, so
// this is discharged silently. Mirrors the AST-walk's own d4324-
// cross-flavor-predicate-conveyor-to-symbolic-ok.C. See
// gcc/cp/contracts-gimple.cc and ~/gimple-contract-analysis.md.
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple -fcontract-symbolic-proofs-gimple" }
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

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

struct io_facility {
  static bool is_opened (io_facility*) conveyor { return true; }
  void open_conveyor () post<conveyor_ctrl_v>(is_opened (this)) {}
  void read_symbolic () pre<symbolic_ctrl_v>(is_opened (this)) {}
};

int main ()
{
  io_facility f;
  f.open_conveyor ();
  f.read_symbolic ();
  return 0;
}
