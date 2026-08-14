// D4324: a non-void-returning conveyor function must contain a return
// statement.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int x) conveyor // { dg-error "conveyor function .* with non-.void. return type must contain a .return. statement" }
{
  int y = x;
  (void) y;
} // { dg-warning "no return statement in function returning non-void" }
