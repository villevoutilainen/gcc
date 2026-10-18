// D4324: a fact a contract_assert establishes about a scalar's value
// ends when the scalar's address is passed to something that may modify
// it -- a pointer, or a non-const reference -- but not a const
// reference.  The AST engine kept such facts.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
using namespace std::contracts;

void clobber (int *) conveyor;
void clobber_ref (int &) conveyor;
void look (const int &) conveyor;

int nonzero_pointer (int x) conveyor
{
  int i = x;
  contract_assert<never_proven_conveyor_v> (i != 0);
  clobber (&i);
  return 7 / i; // { dg-error "divisor .i. not provably nonzero" }
}

int nonzero_reference (int x) conveyor
{
  int i = x;
  contract_assert<never_proven_conveyor_v> (i != 0);
  clobber_ref (i);
  return 7 / i; // { dg-error "divisor .i. not provably nonzero" }
}

int range_pointer (int x) conveyor
{
  int i = x;
  contract_assert<never_proven_conveyor_v> (i > 0);
  clobber (&i);
  return 7 / i; // { dg-error "divisor .i. not provably nonzero" }
}

int const_reference (int x) conveyor
{
  int i = x;
  contract_assert<never_proven_conveyor_v> (i != 0);
  look (i);
  return 7 / i;
}

int stored (int x) conveyor
{
  int i = x;
  contract_assert<never_proven_conveyor_v> (i != 0);
  look (i);
  i = x / 2;
  return 7 / i; // { dg-error "divisor .i. not provably nonzero" }
}
