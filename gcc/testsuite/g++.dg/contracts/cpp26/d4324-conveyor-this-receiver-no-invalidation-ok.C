// D4324/P2680 author correction (2026-08-19), part 2: a conveyor member
// call can never invalidate the object-address validity of its own
// receiver ('this') -- the mandatory conveyor UB-freedom rules already
// forbid 'delete'/'delete this'/an explicit destructor call anywhere in
// its own reachable call graph, so if an object was valid before a
// conveyor member function call, it remains valid after that call. This
// is essential for part 1 (the new is_object_address requirement on
// 'this') to be usable at all: without it, a SECOND conveyor member call
// on the same, already-proven pointer would wrongly need to re-prove
// validity from scratch. The underlying mechanism already existed
// (oa_invalidate_symbolic_facts_for_call_args's own CALLEE_IS_CONVEYOR
// exemption, unconditionally exempting 'this' along with every other
// argument position) -- this test exercises it specifically for the
// newly-added 'this'-receiver obligation, not just the pre-existing
// explicit-reference-parameter case it already covered.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

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

struct S {
  int v;
  int bump () conveyor { v = 5; return v; }
  int bump_again () conveyor { v = 6; return v; }
};

int chained_calls (S *p) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (p))
{
  // Three successive conveyor member calls on the SAME pointer -- each
  // one must still see P as provably valid, not just the first.
  p->bump ();
  p->bump_again ();
  return p->bump ();
}

int main ()
{
  S s{0};
  return chained_calls (&s) == 5 ? 0 : 1;
}
