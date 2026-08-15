// D4324: std::contracts::assume_v is unconditionally ignored -- regardless
// of the TU's -fcontract-evaluation-semantic= -- and assumable, so its
// predicate is always handed to the optimizer as an assumption and never
// evaluated at runtime.  This is unlike mandatory_v, which is never
// ignored and never assumable, under any -fcontract-evaluation-semantic=
// including =ignore (see d4324-assumable.C) -- the two are deliberately
// opposite extremes, not different points on the same "ignored under some
// TU config" spectrum. Built here with =enforce, not =ignore, to prove
// assume_v's own distinction: the predicate is still assumed rather than
// turning into a real runtime check, regardless of the TU's own semantic.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce -O2 -fdump-tree-optimized" }

#include <contracts>

void sink (int);

int f (int x) pre<std::contracts::assume_v>(x > 5)
{
  if (x <= 5)
    sink (x);		// dead: the predicate x > 5 is assumed even under enforce
  return x;
}

// The dead branch guarded by the negation of the assumed predicate is gone,
// even though the TU is built with =enforce rather than =ignore.
// { dg-final { scan-tree-dump-not "sink" "optimized" } }
