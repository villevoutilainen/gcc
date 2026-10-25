// D4324: a 'conveyor(auto)' specialization deduced not to be conveyor is
// checked as the ordinary function it is: here, as such it owes get ()'s
// precondition for its reference parameter, which it can't prove -- its
// implicit preconditions, which trusted that parameter while it was
// presumed conveyor, being gone.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

int plainf (int x) { return x; }

struct W
{
  int v = 0;
  int get () const
    pre<std::contracts::never_proven_conveyor_v> (std::is_object_address (this))
  { return v; }
};

template <class T>
int f (const T &t) conveyor(auto)
{
  return plainf (t.get ()); // { dg-error "cannot prove 'is_object_address'" }
}

int user () { W w; return f (w); }
