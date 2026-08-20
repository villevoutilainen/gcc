// D4324 bounds-proving demo: oa_env_check_call_call_relational_fact_1's
// own range-vs-range fallback (mirroring oa_env_check_call_relational_
// fact_1's identical mechanism, see d4324-conveyor-call-relational-
// range-vs-range-bug.C) reaching OA_PROVEN_FALSE for two calls on two
// DIFFERENT, unrelated-type objects (ruling out aliasing between them,
// which would otherwise invalidate one side's own established call-
// range fact whenever the other side's mutating accessor is called --
// see .claude/plans/lazy-stirring-pearl.md).
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

struct A {
  int data[8];
  int n = 0;
  int size () const conveyor { return n; }
  void resize_to_100 () post<conveyor_ctrl_v>(size () == 100) // { dg-warning "cannot verify postcondition" }
  { n = 100; }
};

struct B {
  int data[8];
  int n = 0;
  int size () const conveyor { return n; }
  void resize_to_5 () post<conveyor_ctrl_v>(size () == 5) // { dg-warning "cannot verify postcondition" }
  { n = 5; }
};

int use_it (A& a, B& b) pre<conveyor_ctrl_v>(a.size () < b.size ()) { return 0; }

int caller (A& a, B& b)
{
  a.resize_to_100 ();
  b.resize_to_5 ();
  return use_it (a, b); // { dg-error "provably violates the precondition" }
                         // { dg-message "is established \[^\n\]*, but the precondition requires" "established fact" { target *-*-* } .-1 }
}

int main () { return 0; }
