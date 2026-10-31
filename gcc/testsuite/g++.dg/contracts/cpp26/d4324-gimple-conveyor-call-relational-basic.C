// D4324: the built-in GIMPLE pass's own mirror of d4324-conveyor-call-
// relational-basic.C (cg_call_rel_fact/cg_get_call_relational, the
// call analogue of the existing cg_rel_fact/cg_get_relational
// mechanism) -- "PARAM OP RECEIVER.ACCESSOR ()" established via a
// function's own declared precondition (self-trust only, never from an
// ordinary branch, on either the AST or the GIMPLE side -- matching
// scope exactly).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple" }

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
  int size () const conveyor { return 5; }
  int get (int n) const conveyor pre<conveyor_ctrl_v>(n < size ()) { return n; }
};

int get_checked (S& s, int i) conveyor pre<conveyor_ctrl_v>(i < s.size ())
{
  return s.get (i);
}

int get_unchecked (S& s, int i) conveyor
{
  return s.get (i); // { dg-warning "cannot verify that .i. satisfies" }
}

int main ()
{
  S s;
  return get_checked (s, 2) // { dg-warning "cannot verify that .2. satisfies" }
	 + get_unchecked (s, 9);
}
