// D4324: dereferencing an unproven pointer to a class -- as a whole object
// (a copy's source, '*(T &) p' to the AST engine), or through a local
// reference bound to it -- is checked like any other dereference.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct T { long s; int n; };

T copy (T *p) conveyor
{
  return *p; // { dg-error "not provably valid" }
}

void assign (T &a, T *p) conveyor
{
  a = *p; // { dg-error "not provably valid" }
}

long via_reference (T *p) conveyor
{
  T &r = *p;
  return r.s; // { dg-error "not provably valid" }
}
