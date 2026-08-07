// D4324: one-way trust between the two control-object flavors --
// conveyor-established facts are trustworthy enough for symbolic's own
// checks to rely on (backed by real UB-freedom verification), but a
// symbolic-established fact is never verified against anything (purely
// trusted, the same way any named-predicate axiom is), so it must never
// satisfy a *conveyor* obligation -- that would silently weaken the
// guarantee conveyor is supposed to provide.  open_symbolic()'s
// SYMBOLIC-flavored postcondition establishes is_opened(this); read_
// conveyor()'s CONVEYOR-flavored precondition requires the same fact on
// the same object -- must report "cannot verify", not silently pass.
// See .claude/plans/well-we-last-discussed-ethereal-duckling.md.
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
  void open_symbolic () post<symbolic_ctrl_v>(is_opened (this)) {}
  void read_conveyor () pre<conveyor_ctrl_v>(is_opened (this)) {}
};

void caller ()
{
  io_facility f;
  f.open_symbolic ();
  f.read_conveyor (); // { dg-warning "cannot verify" }
}

int main () { caller (); return 0; }
