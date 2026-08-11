// D4324, Increment Q, revised: an ordinary implicit destructor call, at
// the end of a local variable's scope, used to be exempt from every
// conveyor restriction outright (only an explicitly-spelled destructor
// call was restricted). Superseded: the callee-must-be-conveyor rule
// added later now covers *every* call reached from conveyor-restricted
// code, including an implicit one -- routed through the exact same
// build_over_call path as any other call, with the sole, structural
// exemption being a genuinely trivial special member (which is diverted
// before reaching that path at all, so never needs the keyword). S's
// own destructor is non-trivial (user-provided), so it must now be
// declared conveyor itself for this implicit call to remain legal --
// see d4324-conveyor-callee-implicit-dtor-nonconveyor-bad.C for the
// negative case this change introduces, and d4324-conveyor-callee-
// trivial-dtor-ok.C for confirmation that a genuinely trivial implicit
// destructor still needs nothing.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct S { int v; ~S () conveyor {} };

int f () conveyor
{
  S s{1};
  return s.v;
}

int main () { return f () - 1; }
