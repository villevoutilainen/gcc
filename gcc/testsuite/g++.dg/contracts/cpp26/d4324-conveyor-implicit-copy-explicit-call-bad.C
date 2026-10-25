// D4324: a caller of a conveyor defaulted copy assignment that reads its
// source must prove the source an object.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct T { long s; int n; };

void ok (T &a, T &b) conveyor
{
  a.operator= (b);
}

void bad (T &a, T *p) conveyor
{
  a.operator= (*p); // { dg-error "cannot prove 'is_object_address'|not provably valid" }
}
