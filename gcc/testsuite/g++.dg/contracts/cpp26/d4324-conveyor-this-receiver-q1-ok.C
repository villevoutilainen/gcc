// D4324/P2680 author correction (2026-08-19), refined by the later
// soundness fix in ~/soundness-fixes-for-conveyors.md: a member conveyor
// call's own receiver ('this' of the callee) carries the same implicit
// is_object_address obligation an explicit reference parameter does --
// 'this' is never an unconditional axiom, and neither is an ordinary
// (non-conveyor) function's own reference parameter. This test covers
// the OK shapes: a receiver that's provably valid, via each of the ways
// oa_provable_p already recognizes one (including an ordinary function
// giving an explicit precondition of its own for its reference
// parameter, the only way it can ever discharge such a call).
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
};

// A local variable's own address is always provable.
int via_local ()
{
  S s{1};
  return s.bump ();
}

// An explicitly-asserted pointer parameter.
int via_pointer_precondition (S *p) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (p))
{
  return p->bump ();
}

// A reference parameter of a non-conveyor function: no automatic
// self-trust (D4324/P2680 soundness fix) -- needs its own explicit
// is_object_address(&r) precondition to discharge r.bump()'s own
// implicit receiver obligation.
int via_reference_parameter (S &r)
  pre<conveyor_ctrl_v>(std::is_object_address (&r))
{
  return r.bump ();
}

// 'this' of a conveyor member function, calling ANOTHER conveyor
// member function on itself -- trusted throughout its own body.
struct T {
  S s;
  int nested () conveyor { return s.bump (); }
};

int main ()
{
  S s{1};
  T t{{1}};
  bool ok = via_local () == 5 && via_pointer_precondition (&s) == 5
	    && via_reference_parameter (s) == 5 && t.nested () == 5;
  return ok ? 0 : 1;
}
