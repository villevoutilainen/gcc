// D4324: the allowed direction of the one-way trust, for the ptr->field
// range map -- see d4324-cross-flavor-field-range-symbolic-to-conveyor-
// unknown.C for the forbidden direction.  produce_count_conveyor()'s
// CONVEYOR-flavored postcondition establishes this->count in [40,100);
// consume_count_symbolic()'s SYMBOLIC-flavored precondition requires
// this->count in [20,1000) on the same object -- a conveyor-established
// fact is trustworthy enough for symbolic's own check to rely on, so
// this is discharged silently.  See .claude/plans/well-we-last-
// discussed-ethereal-duckling.md.
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

struct thing {
  int count;
  void produce_count_conveyor ()
    post<conveyor_ctrl_v>(this->count >= 40
			  && this->count < 100)
  { count = 55; }
  void consume_count_symbolic ()
    pre<symbolic_ctrl_v>(this->count >= 20 && this->count < 1000)
  { }
};

int main ()
{
  thing t;
  t.produce_count_conveyor ();
  t.consume_count_symbolic ();
  return 0;
}
