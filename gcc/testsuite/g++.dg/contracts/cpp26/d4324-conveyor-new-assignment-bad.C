// D4324: the counterpart of d4324-conveyor-new-assignment-ok.C -- only an
// allocating new-expression gives the pointer it's assigned to an object.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
#include <new>

struct A { int x; int get () const conveyor { return x; } };

// The standard placement new is only as good as its argument.
int f1 (void *buf)
{
  A *p;
  p = new (buf) A ();
  return p->get (); // { dg-error "cannot prove .is_object_address. for .p." }
}

// Not on every path.
int f2 (int c, A *q)
{
  A *p;
  if (c)
    p = new A ();
  else
    p = q;
  return p->get (); // { dg-error "cannot prove .is_object_address. for .p." }
}

// Not any more.
int f3 (A *q)
{
  A *p;
  p = new A ();
  p = q;
  return p->get (); // { dg-error "cannot prove .is_object_address. for .p." }
}
