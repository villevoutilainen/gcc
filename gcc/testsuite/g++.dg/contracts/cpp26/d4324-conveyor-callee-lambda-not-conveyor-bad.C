// D4324: the negative counterpart of d4324-conveyor-callee-lambda-
// conveyor-ok.C -- a lambda stored in a variable (not immediately
// invoked) and NOT declared 'conveyor' is rejected the same way any
// other non-conveyor callee is. See .claude/plans/well-we-last-
// discussed-ethereal-duckling.md.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int x) conveyor
{
  auto add_one = [] (int y) { return y + 1; };
  add_one (x); // { dg-error "not declared .conveyor." }
  return 0;
}

int main () { return f (1); }
