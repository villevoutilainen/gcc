// D4324/P2680: -fcontract-conveyor-proofs, oa_handle_call_conveyor_
// call_range_obligation's own OA_RANGE_DISJOINT case -- a bare RANGE
// precondition on a call's own return value (not a relational
// comparison against another parameter/call, that's d4324-conveyor-
// call-relational-range-vs-range-bug.C's own territory), established
// via a postcondition to lie entirely outside the required interval.
// Exercises the established-fact follow-up note's own call-range
// branch specifically (see .claude/plans/lazy-stirring-pearl.md).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

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

struct thing {
  int count = 0;
  int get_count () const conveyor { return count; }
  void set_to_200 () post<conveyor_ctrl_v>(get_count () == 200) // { dg-warning "cannot verify postcondition" }
  { count = 200; }
};

void consume (thing& t)
  pre<conveyor_ctrl_v>(t.get_count () >= 20 && t.get_count () <= 99)
{
  (void) t;
}

void caller (thing& t)
{
  t.set_to_200 ();
  consume (t); // { dg-error "provably violates the precondition" }
               // { dg-message "is established \[^\n\]*, but the precondition requires" "established fact" { target *-*-* } .-1 }
}

int main () { return 0; }
