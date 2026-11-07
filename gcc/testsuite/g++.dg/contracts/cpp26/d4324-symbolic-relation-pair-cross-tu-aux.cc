// The other translation unit of d4324-symbolic-relation-pair-cross-tu.C.
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects -fcontract-symbolic-runtime-checks -fcontract-evaluation-semantic=observe" }

#include "d4324-symbolic-relation-pair-cross-tu.h"

long
sum (It first, It last)
{
  long s = 0;
  for (; first != last; ++first)
    s += *first;
  return s;
}
