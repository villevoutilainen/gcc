// D4324: a conveyor function's body may not contain a reinterpret_cast.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int x) conveyor
{
  long p = reinterpret_cast<long> (&x); // { dg-error "reinterpret_cast. not permitted in a conveyor function or predicate" }
  return (int) p;
}
