// D4324: a functor's own operator() is just an ordinary member function
// found via lookup -- calling it from conveyor-restricted code needs it
// to be declared 'conveyor', the same as any other call, and it works
// once so declared (confirms functor calls route through the same
// build_over_call path as ordinary calls, with no extra indirection at
// the call site to worry about). See .claude/plans/well-we-last-
// discussed-ethereal-duckling.md.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct adder
{
  int operator() (int x) const conveyor { return x; }
};

int f (int x) conveyor
{
  adder a{};
  return a (x);
}

int main () { return f (1) - 1; }
