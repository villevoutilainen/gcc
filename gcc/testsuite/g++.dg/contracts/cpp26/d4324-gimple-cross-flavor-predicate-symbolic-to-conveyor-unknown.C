// The built-in GIMPLE-pass engine's own one-way trust between the two
// control-object flavors, for named predicates: a symbolic-established
// fact is purely trusted, never verified against anything, so it must
// never satisfy a *conveyor* obligation -- that would silently weaken
// the guarantee conveyor is supposed to provide. open_symbolic()'s
// SYMBOLIC-flavored postcondition establishes is_opened(this);
// read_conveyor()'s CONVEYOR-flavored precondition requires the same
// fact on the same object -- must report "cannot verify", not silently
// pass. Mirrors the AST-walk's own d4324-cross-flavor-predicate-
// symbolic-to-conveyor-unknown.C. See gcc/cp/contracts-gimple.cc and
// ~/gimple-contract-analysis.md.
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
  static bool is_opened (io_facility*) { return true; }
  void open_symbolic () post<symbolic_ctrl_v>(is_opened (this)) {}
  void read_conveyor () pre<conveyor_ctrl_v>(is_opened (this)) {}
};

void caller ()
{
  io_facility f;
  f.open_symbolic ();
  f.read_conveyor (); // { dg-warning "cannot verify that .*is_opened.*holds" }
}

int main () { caller (); return 0; }
