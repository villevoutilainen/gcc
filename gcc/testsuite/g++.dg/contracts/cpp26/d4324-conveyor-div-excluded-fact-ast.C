// D4324 (P4445 2.2.2.7): an established 'v != K' fact -- a precondition or
// a never-proven contract assertion -- excludes the value K: 'y != -1' or
// 'x != INT_MIN' proves a division free of overflow, which no bound on y or
// x can express.  The GIMPLE engine didn't use such a fact; the engines
// agree now.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=ast" }

#include <contracts>
#include <climits>

void g (int x, int y)
{
  contract_assert<std::contracts::never_proven_conveyor_v> (y != 0 && y != -1);
  contract_assert<std::contracts::never_proven_conveyor_v> (x / y > 0);
}

int q (int x, int y) conveyor
  pre<std::contracts::conveyor_assert_v> (y != 0 && y != -1)
{ return x / y; }

int r (int x, int y) conveyor
  pre<std::contracts::conveyor_assert_v> (y != 0 && x != INT_MIN)
{ return x % y; }

int bad (int x, int y) conveyor
  pre<std::contracts::conveyor_assert_v> (y != 0 && y != -2)
{ return x / y; }	// { dg-error "not provably free of overflow" }
