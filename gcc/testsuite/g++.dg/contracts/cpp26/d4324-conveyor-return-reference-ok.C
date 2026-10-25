// D4324: what a conveyor function may return by reference, and where a
// reference a conveyor call returns may be used.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct V { int n = 0; int size () const conveyor { return n; } };
V &id (V &x) conveyor { return x; }
const V &cid (const V &x) conveyor { return x; }
V &param (V &x) conveyor { return x; }
V &alias_param (V &x) conveyor { V &y = x; return y; }
V &forwarded (V &x) conveyor { return id (x); }
const V &const_param (const V &x) conveyor { return x; }

int direct (V &x) conveyor { return id (x).n; }
int same_full_expression () conveyor { return cid (V{3}).n; }
int bound_to_local ()
{
  V x{4};
  const V &r = id (x);
  return r.size ();
}

int main ()
{
  V v{2};
  if (param (v).n + alias_param (v).n + forwarded (v).n + const_param (v).n
      + direct (v) + same_full_expression () + bound_to_local () != 17)
    __builtin_abort ();
}
