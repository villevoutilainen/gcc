// D4324: the allowed direction of the one-way trust between the two
// control-object flavors (see d4324-cross-flavor-predicate-symbolic-to-
// conveyor-unknown.C for the forbidden direction) -- open_conveyor()'s
// CONVEYOR-flavored postcondition establishes is_opened(this); read_
// symbolic()'s SYMBOLIC-flavored precondition requires the same fact on
// the same object.  A conveyor-established fact is backed by real UB-
// freedom verification, so it's trustworthy enough for symbolic's own,
// purely-axiomatic check to rely on -- discharged silently.  See
// .claude/plans/well-we-last-discussed-ethereal-duckling.md.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs -fcontract-symbolic-proofs" }

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
