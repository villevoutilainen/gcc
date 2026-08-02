// D4324, Increment P: a conveyor function may not odr-use a non-const
// namespace-scope variable.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int g;

int f () conveyor
{
  int r = g; // { dg-error "use of non-const variable .g. with static storage duration not permitted in a conveyor function or predicate" }
  return r;
}
