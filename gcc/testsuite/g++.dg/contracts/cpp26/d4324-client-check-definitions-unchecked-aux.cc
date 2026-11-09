// The "foreign library" of d4324-client-check-definitions-unchecked.C.
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects -fcontracts-definition-check=off -fcontracts-client-check=pre -fcontract-evaluation-semantic=observe" }

#include "d4324-client-check-definitions-unchecked.h"

int
vendor::f (int x) pre<std::contracts::default_v> (x > 0)
{
  return x;
}
