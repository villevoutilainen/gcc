// D4324: a call through a raw function-pointer value is banned outright
// from conveyor-restricted code -- there is no fixed FUNCTION_DECL at
// the call site whose 'conveyor' declaration could even be consulted.
// See .claude/plans/well-we-last-discussed-ethereal-duckling.md.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int helper (int x) conveyor { return x; }

int f (int x) conveyor
{
  int (*fp) (int) = &helper;
  fp (x); // { dg-error "call through a function pointer" }
  return 0;
}

int main () { return f (1); }
