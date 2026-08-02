// D4324, Increment P: a conveyor function may not odr-use a non-const
// static data member.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct S { static int m; };
int S::m = 0;

int f () conveyor
{
  int r = S::m; // { dg-error "use of non-const variable .S::m. with static storage duration not permitted in a conveyor function or predicate" }
  return r;
}
