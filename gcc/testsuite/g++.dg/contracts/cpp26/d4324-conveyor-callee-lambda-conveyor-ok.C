// D4324: the new grammar addition -- a lambda's own operator() can now
// be declared 'conveyor' (e.g. '[...]() conveyor { ... }'), the same
// trailing function-specifier an ordinary named function already
// supports, letting a lambda STORED in a variable (not an immediately-
// invoked closure expression) be called from conveyor-restricted code
// like any other callable. See .claude/plans/well-we-last-discussed-
// ethereal-duckling.md.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int x) conveyor
{
  auto add_one = [] (int y) conveyor { return y; };
  return add_one (x);
}

int main () { return f (1) - 1; }
