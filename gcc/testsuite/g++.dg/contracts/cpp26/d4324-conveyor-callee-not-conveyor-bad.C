// D4324: the core fix -- a call from a conveyor-declared function's own
// body must target a function that is itself declared 'conveyor'.
// Without this, the mandatory UB-freedom guarantee only ever covered
// the literal text of the calling function's own body, never anything
// it called into. See .claude/plans/well-we-last-discussed-ethereal-
// duckling.md.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int helper (int x) { return x + 1; }

int f (int x) conveyor
{
  helper (x); // { dg-error "not declared .conveyor." }
  return 0;
}

int main () { return f (1); }
