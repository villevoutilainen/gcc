// D4324: mandatory_v is never ignored and never assumable, under any
// -fcontract-evaluation-semantic= including =ignore -- unlike
// default_control/review, there is no build configuration that turns its
// precondition into an unchecked optimizer assumption instead of a real
// runtime check. Even built with =ignore, the predicate is still a real
// check (see mandatory's own operator(), which unconditionally logs and
// terminates on failure): the branch guarded by its negation is not dead
// code, and must survive optimization.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=ignore -O2 -fdump-tree-optimized" }

#include <contracts>

void sink (int);

int f (int x) pre<std::contracts::mandatory_v>(x > 5)
{
  if (x <= 5)
    sink (x);		// live: mandatory_v's precondition is a real check,
			// never an assumption, even under =ignore
  return x;
}

// { dg-final { scan-tree-dump "sink" "optimized" } }
