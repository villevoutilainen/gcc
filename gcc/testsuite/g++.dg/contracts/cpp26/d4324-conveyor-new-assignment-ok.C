// D4324: a new-expression assigned to a pointer, not only one initializing
// it, gives the pointer an object: 'p = new T ();' has the new-expression
// under a conversion to P's type.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
#include <new>

struct A { int x; int get () const conveyor { return x; } };
struct B { int x; B (); int get () const conveyor { return x; } };
struct Alloc { };
void *operator new (std::size_t n, Alloc &) { return ::operator new (n); }

int f1 () { A *p; p = new A; return p->get (); }
int f2 () { A *p = nullptr; p = new A (); return p->get (); }
int f3 () { B *p; p = new B (); return p->get (); }
int f4 (Alloc &fa) { A *p; p = new (fa) A (); return p->get (); }

int f5 (int c)
{
  A *p;
  if (c)
    p = new A ();
  else
    p = new A {1};
  return p->get ();
}

// The shape of BDE's test drivers.
int f6 (Alloc &fa, int c)
{
  A *objPtr;
  switch (c)
    {
    case 0: objPtr = new (fa) A (); break;
    case 1: objPtr = new (fa) A {1}; break;
    default: objPtr = new A {2}; break;
    }
  A &mX = *objPtr;
  const A &X = mX;
  return mX.get () + X.get ();
}
