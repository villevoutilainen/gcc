// D4324: as d4324-conveyor-auto-defaulted-comparison.C, with a member whose
// operator== isn't conveyor: the defaulted comparison deduces as not
// conveyor, and a conveyor function can't call it.
// { dg-do compile { target c++26 } }
// { dg-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct M
{
  int v;
  friend bool operator== (const M &x, const M &y) { return x.v == y.v; }
};

template <class T>
struct It
{
  T m;
  friend bool operator== (const It &, const It &) conveyor (auto) = default;
};

bool
eq (const It<M> &a, const It<M> &b) conveyor
{
  return a == b; // { dg-error "not declared 'conveyor'" }
}
