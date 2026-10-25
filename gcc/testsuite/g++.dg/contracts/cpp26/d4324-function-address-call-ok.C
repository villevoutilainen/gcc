// D4324: a call through '&f' or '&S::f' names a function, whose contracts
// are its own -- not a callable object's declaration-level ones (this used
// to ICE in object_contract_callable_pieces).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct S
{
  int n = 0;
  int size () const conveyor { return n; }
};

int f (int x) pre (x > 0) { return x; }

int main ()
{
  (void) sizeof ((S ().*&S::size) (), 0);
  S s;
  s.n = 3;
  int a = (s.*&S::size) ();
  int b = (&f) (1);
  int c = (*f) (2);
  if (a + b + c != 6)
    __builtin_abort ();
}
