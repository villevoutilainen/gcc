// D4324: a conveyor function's body may not contain a new-expression or a
// delete-expression.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int x) conveyor
{
  int* p = new int (x); // { dg-error "new.-expression not permitted in a conveyor function or predicate" }
  return *p;
}

int g (int* x) conveyor
{
  delete x; // { dg-error "delete.-expression not permitted in a conveyor function or predicate" }
  return 0;
}
