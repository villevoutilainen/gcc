// D4324: a plain aggregate local, whose destructor is genuinely
// trivial (no user-provided special members anywhere), still needs
// nothing -- a trivial special member is diverted to a dedicated
// trivial-call path before reaching build_over_call's general callee-
// must-be-conveyor check at all. See .claude/plans/well-we-last-
// discussed-ethereal-duckling.md.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct S { int v; };

int f () conveyor
{
  S s{1};
  return s.v;
}

int main () { return f () - 1; }
