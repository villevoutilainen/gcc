// D4324: the GIMPLE engine checks a conveyor function's own
// postcondition at its exit, against the facts merged over every
// return, like the AST engine: a provably false conjunct is an error, an
// unproven one a warning.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple -fcontract-conveyor-proofs" }

#include <contracts>
using namespace std::contracts;

int proven (int x) conveyor post<conveyor_assert_v> (r: r > 0)
{
  if (x > 3)
    return x;
  return 7;
}

int unverified (int x) conveyor post<conveyor_assert_v> (r: r > 5) // { dg-warning "cannot verify postcondition condition .\\(r > 5\\)." }
{
  if (x > 3)
    return x;
  return 7;
}

int violated (int x) conveyor post<conveyor_assert_v> (r: r < 0) // { dg-error "postcondition condition .\\(r < 0\\). is provably false" }
{ // { dg-message "established >= 4" "" { target *-*-* } .-1 }
  if (x > 3)
    return x;
  return 7;
}

int from_precondition (int x) conveyor
  pre<conveyor_assert_v> (x > 2)
  post<conveyor_assert_v> (r: r > 1)
{
  return x;
}

int trusted (int x) conveyor post<never_proven_conveyor_v> (r: r > 100)
{
  return x;
}
