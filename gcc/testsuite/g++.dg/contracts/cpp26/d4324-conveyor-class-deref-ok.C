// D4324: references bound to provable objects stay readable.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct T { long s; int n; };

T &get (T &a) conveyor { return a; }

long from_call (T &a) conveyor
{
  T &r = get (a);
  return r.s;
}

long from_param (T &a) conveyor
{
  T &r = a;
  T &r2 = r;
  return [&] { return r2.s; } ();
}

bool from_locals () conveyor
{
  T t{1, 2};
  T &r = t;
  const T &c = T{3, 4};
  T u = r;
  return r.s == 1 && c.s == 3 && u.n == 2;
}

int main ()
{
  T t{5, 6};
  if (from_param (t) != 5 || from_call (t) != 5 || !from_locals ())
    __builtin_abort ();
}
