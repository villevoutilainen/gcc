// D4324, Increment Q: the same two rejected shapes (pseudo-destructor
// call, explicit destructor call on a class type) stay accepted outside
// a conveyor function -- confirming the restriction is conveyor-scoped,
// not a blanket ban.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

typedef int I;
struct S { int v; ~S () {} };

int g (int* p)
{
  p->I::~I ();
  return 0;
}

int f (S& obj)
{
  obj.~S ();
  return obj.v;
}

int main ()
{
  int x = 5;
  g (&x);
  S s{7};
  return f (s) - 7;
}
