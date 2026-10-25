// D4324: a reference a conveyor function returns must outlive the call
// and be provably an object; a reference a conveyor call returns, given a
// temporary it may hand back, doesn't outlive that temporary's
// full-expression.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct V { int n = 0; int size () const conveyor { return n; } };
V &id (V &x) conveyor { return x; }
const V &cid (const V &x) conveyor { return x; }

V &alias_local () conveyor
{
  V v{};
  V &r = v;
  return r; // { dg-error "does not outlive it" }
}

V &unproven (V *p) conveyor
{
  return *p; // { dg-error "not provably to an object" }
}

V &through_call () conveyor
{
  V v{};
  return id (v); // { dg-error "does not outlive it" }
}

const V &temporary () conveyor
{
  return cid (V{}); // { dg-error "does not outlive it" }
}

int bound_then_read () conveyor
{
  const V &r = cid (V{});
  return r.n; // { dg-error "not provably valid" }
}

int bound_then_called ()
{
  const V &r = cid (V{});
  return r.size (); // { dg-error "cannot prove 'is_object_address'" }
}
