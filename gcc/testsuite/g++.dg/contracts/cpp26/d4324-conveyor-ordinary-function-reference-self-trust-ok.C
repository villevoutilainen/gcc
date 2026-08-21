// D4324/P2680 soundness fix (see ~/soundness-fixes-for-conveyors.md): an
// ORDINARY (non-conveyor-declared) function's own reference parameter
// gets NO automatic self-trust at all when forwarded from its OWN
// precondition/postcondition/assert text to another conveyor call's
// reference parameter -- only a conveyor-declared function's own
// reference parameters/'this' carry an implicit, compiler-synthesized
// is_object_address precondition (oa_synthesize_implicit_reference_
// safety_preconditions), discharged by ITS OWN callers; an ordinary
// function has no such synthesized precondition; forwarding its own
// reference parameter to a conveyor callee is exactly as unproven as
// forwarding an unrelated pointer would be. 'forward' below is the
// FORWARDING function, still not itself conveyor-declared, but now
// gives an explicit is_object_address(&y) precondition of its own so
// the call to use_val(y) can be discharged -- the only way a non-
// conveyor function can ever hand such a proof to a conveyor callee.
//
// Deliberately uses a CONST reference target (Q1 only, no Q2): Q2's own
// ownership check unconditionally returns false for ANY parameter (not
// just 'this') reached from inside a precondition/assert's own
// condition text -- predicate/assert context never "owns" anything, a
// separate, pre-existing, deliberate restriction unrelated to this fix
// (see oa_reference_owned_p's own "PREDICATE/ASSERT context" comment).
// A non-const-reference-target version of this test would fail for that
// unrelated reason, not the one this test is about.
//
// Originally dg-do compile only: this exact shape (a precondition calling
// a nested conveyor function from its own condition text) also tripped a
// separate, genuinely pre-existing runtime codegen bug in
// build_predicate_arg_struct_var (a reference parameter's own args-struct
// field was computed via one level of indirection too many, see that
// function's own comment) -- now fixed, so this runs for real too.
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

int use_val (const int &x) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (&x))
{
  return x;
}

// Plain (non-conveyor) function whose own precondition forwards its own
// reference parameter to another conveyor function's const reference
// parameter.
int forward (int &y)
  pre<conveyor_ctrl_v>(std::is_object_address (&y))
  pre<conveyor_ctrl_v>(use_val (y) >= 0)
{ return y; }

int main () { int v = 1; return forward (v) - 1; }
