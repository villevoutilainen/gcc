// D4324: a conveyor function's body may not contain a throw-expression.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int x) conveyor
{
  if (x < 0)
    throw x; // { dg-error "throw.-expression not permitted in a conveyor function or predicate" }
  return x;
}
