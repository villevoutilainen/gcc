// D4324: the negative counterpart of d4324-conveyor-callee-functor-
// ok.C -- a functor whose operator() is NOT declared 'conveyor' is
// rejected the same way any other non-conveyor callee is. See
// .claude/plans/well-we-last-discussed-ethereal-duckling.md.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct adder
{
  int operator() (int x) const { return x + 1; }
};

int f (int x) conveyor
{
  adder a{};
  a (x); // { dg-error "not declared .conveyor." }
  return 0;
}

int main () { return f (1); }
