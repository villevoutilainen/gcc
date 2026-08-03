// D4324: declaration-level contracts on callable-typed object
// declarations, extended to class-type callables (see
// .claude/plans/stateless-jumping-shore.md) -- a class type can have
// both an operator() (the one resolved, uniquely, at the declaration)
// *and* a conversion operator to a function pointer, which competes
// with operator() in real overload resolution at an actual call site
// (build_op_call, call.cc).  Per direct user feedback ("if the first
// call site is valid, and the contract has a different meaning for a
// second call, reject that second call... the language shouldn't
// guess"), a call that resolves through anything other than the
// operator() the declaration's contract was checked against is a hard
// compile-time error at that call site, not a silent skip and not a
// silently-different check.
//
// wrapper's own operator() takes two Incompatible objects, which int
// cannot convert to at all -- so operator() isn't even a viable
// candidate for w (1, 2), forcing resolution through the *surrogate*
// call function instead (the conversion operator's real int(*)(int,int)
// target): exactly the divergence this feature must reject, since
// resolve_single_call_operator (used at the declaration) considers
// operator() alone, regardless of whether it's viable for any
// particular call's arguments.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fsyntax-only" }

#include <contracts>

typedef int (*fnptr) (int, int);

int real_add (int a, int b) { return a + b; }

struct Incompatible {};

struct wrapper {
  int operator() (Incompatible, Incompatible) const { return 0; }
  operator fnptr () const { return &real_add; }
};

wrapper w pre<> (true);

int main ()
{
  w (1, 2); // { dg-error "call does not resolve through the .operator\\(\\). declared for .w., so its contract specifier does not apply to this call" }
  return 0;
}
