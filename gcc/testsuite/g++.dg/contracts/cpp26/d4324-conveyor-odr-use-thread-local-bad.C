// D4324, Increment P: a thread_local variable is never permitted to be
// odr-used in a conveyor function, even if it is also const.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

thread_local const int g = 5;

int f () conveyor
{
  int r = g; // { dg-error "use of .thread_local. variable .g. not permitted in a conveyor function or predicate" }
  return r;
}
