// D4324/P2680: a genuinely pre-existing gap, found (and fixed) while
// implementing the 'this'-receiver Q1 correction, but independent of it:
// an ORDINARY (non-conveyor-declared) function's own reference
// parameter, forwarded from its OWN precondition/postcondition/assert
// text to another conveyor call's reference parameter, needs the same
// self-trust a conveyor-declared function's own reference parameter
// already gets -- previously this was scoped to DECL_DECLARED_CONVEYOR_P
// of the FORWARDING function, so this exact shape failed to prove
// is_object_address on stock, unmodified GCC, confirmed by direct
// testing. Widened to unconditional (every function's own reference
// parameters, not just a conveyor-declared one's), matching a bound
// reference's own "valid for its entire lifetime" language guarantee,
// which holds regardless of whether the referencing function happens to
// be conveyor-declared.
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
int forward (int &y) pre<conveyor_ctrl_v>(use_val (y) >= 0) { return y; }

int main () { int v = 1; return forward (v) - 1; }
