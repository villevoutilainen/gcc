// D4324: declaration-level contracts on a pointer-to-member-function
// *parameter* (as opposed to a namespace-scope variable), mirroring
// d4324-object-decl-callable-enforcement-param.C for the ordinary
// function-pointer case -- confirms the check functions (and the
// synthesized 'this' they thread through) work correctly when OBJDECL
// is function-local, rebuilt fresh each time the enclosing function's
// own parameter comes into existence, and that 'this' in the condition
// correctly denotes the object supplied at each call made from within
// the enclosing function's own body.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

struct labeled {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  { if (ctx.check ()) return; violated = true; }
  static bool violated;
};
bool labeled::violated = false;

struct C {
  int field;
  int member (int x) { return field + x; }
};

void
invoke (C& obj, int (C::*p)(int x) pre<labeled{}> (this->field > 0 && x > 0))
{
  (obj.*p) (1);
}

int
main ()
{
  C good;
  good.field = 1;
  invoke (good, &C::member);
  if (labeled::violated)
    __builtin_abort ();

  // A second, distinct call through a *different* invocation of the
  // same enclosing function still checks correctly.
  invoke (good, &C::member);
  if (labeled::violated)
    __builtin_abort ();

  // The object's own state (field <= 0) violates the precondition.
  C bad;
  bad.field = 0;
  invoke (bad, &C::member);
  if (!labeled::violated)
    __builtin_abort ();

  return 0;
}
