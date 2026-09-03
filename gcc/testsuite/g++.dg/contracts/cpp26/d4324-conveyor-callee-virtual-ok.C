// D4324: a genuinely virtual call to a 'conveyor'-declared virtual
// function is permitted from conveyor-restricted code. This used to be
// banned outright (see this test's own prior incarnation,
// d4324-conveyor-callee-virtual-bad.C, in version control) because the
// compiler had no way to know every override was itself conveyor.
// check_final_overrider (search.cc) now rejects, as ill-formed, any
// override of a 'conveyor' virtual function that is not itself declared
// 'conveyor' -- conveyor-ness is never automatically inherited by an
// override (unlike an ordinary contract, see
// maybe_inherit_virtual_contract), so every override in the hierarchy
// must satisfy this explicitly, all the way down. That makes it a
// checked invariant that whichever override is actually invoked at
// runtime through Base::f's own vtable slot is conveyor too, so the
// call below needs no further proof beyond the ordinary callee-must-be-
// conveyor rule (see d4324-conveyor-override-not-conveyor-bad.C for the
// override-side rejection this relies on).
//
// Base/Derived's own destructors are deliberately left non-conveyor:
// declaring a virtual destructor conveyor drags in its own compiler-
// generated "deleting destructor" clone's call to operator delete
// (irrelevant to what this test checks) -- a separate, disclosed
// follow-on gap, not fixed here.
// See .claude/plans/well-we-last-discussed-ethereal-duckling.md.
//
// P2680 author correction (2026-08-19): a member conveyor call's own
// receiver now carries the same implicit is_object_address obligation
// an explicit reference parameter always has (see oa_handle_call_
// precondition_obligation's own is_this_parameter block) -- 'b', G's
// own PLAIN POINTER parameter, is exactly as opt-in as it would be
// passing '*b' to an explicit reference parameter elsewhere (Q1 has
// always been opt-in-only for pointers), so G now needs this explicit
// precondition to keep compiling; that requirement is orthogonal to
// this test's own actual point (a genuinely virtual call is no longer
// banned), which is otherwise unaffected. This also proves out a
// second, independent fix: once the virtual-call ban was lifted, the
// vtable pointer's own dereference (compiler-synthesized by
// build_vfn_ref, never something user code can name or annotate) needed
// its own exemption from the ordinary is_object_address deref-provability
// check -- see oa_scan_array_bounds_in_expr's OBJ_TYPE_REF case.
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

struct Base
{
  virtual int f (int x) conveyor { return x; }
  virtual ~Base () {}
};

struct Derived : Base
{
  int f (int x) conveyor override { return x; }
};

int g (Base *b) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (b))
{
  return b->f (1);
}

int main () { Derived d; return g (&d) - 1; }
