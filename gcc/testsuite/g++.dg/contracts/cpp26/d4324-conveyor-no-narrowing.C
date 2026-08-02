// D4324: a conveyor function's body may not contain a narrowing conversion
// (checked at brace-init sites; see the plan for known gaps in coverage of
// other conversion-expression contexts).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (long x) conveyor
{
  int a[] = {x}; // { dg-error "narrowing conversion of .x. from .long int. to .int. not permitted in a conveyor function or predicate" }
  return a[0];
}
