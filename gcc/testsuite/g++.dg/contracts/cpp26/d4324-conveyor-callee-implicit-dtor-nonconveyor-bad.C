// D4324: an implicit destructor call, at the end of a local variable's
// scope, is now ALSO subject to the callee-must-be-conveyor rule, the
// same as any other call reached from conveyor-restricted code -- this
// is an intentional behavior change (see d4324-conveyor-implicit-dtor-
// ok.C's own updated comment for the superseded, narrower prior
// behavior). Only a genuinely trivial destructor is structurally exempt
// (diverted before reaching the general call-building path at all --
// see d4324-conveyor-callee-trivial-dtor-ok.C); S's own destructor here
// is user-provided (non-trivial), so it needs the keyword like any
// other callee. See .claude/plans/well-we-last-discussed-ethereal-
// duckling.md.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct S { int v; ~S () {} };

int f () conveyor
{
  S s{1}; // { dg-error "not declared .conveyor." }
  return s.v;
}

int main () { return f () - 1; }
