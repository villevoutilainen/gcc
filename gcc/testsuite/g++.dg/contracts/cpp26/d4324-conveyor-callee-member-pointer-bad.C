// D4324: the same ban as d4324-conveyor-callee-function-pointer-bad.C,
// for a call through a bound pointer-to-member-function -- also has no
// fixed FUNCTION_DECL at the call site. See .claude/plans/well-we-last-
// discussed-ethereal-duckling.md.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct S
{
  int helper (int x) conveyor { return x; }
};

int f (S *s, int x) conveyor
{
  int (S::*pmf) (int) = &S::helper;
  (s->*pmf) (x); // { dg-error "call through a function pointer" }
  return 0;
}

int main () { S s; return f (&s, 1); }
