// D4324: an implicitly-declared copy assignment of a class of scalars is
// conveyor (P2680 9.2.3); taking its address defines it, and its body's
// read of the source is trusted under the defaulted member's implicit
// is_object_address precondition.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct T { long s; int n; };

int main ()
{
  typedef T& (T::*P) (const T&);
  P p = &T::operator=;
  T a{1, 2}, b{3, 4};
  (a.*p) (b);
  if (a.s != 3 || a.n != 4)
    __builtin_abort ();
}
