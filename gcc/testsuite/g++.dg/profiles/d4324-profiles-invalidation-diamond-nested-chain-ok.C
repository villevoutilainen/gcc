// P3446R0/P4296R0 Invalidation profile: a diamond nested one hop
// INSIDE a copy chain (q's own binding is diamond-shaped; p = q;)
// rather than at the top level. This is a documented, narrower
// residual scope limit of the diamond fix, not a claim of full
// precision through an arbitrarily deep alias chain: p's own binding
// resolution conservatively declines to establish a container at all
// once it finds more than one reaching definition for q at the copy
// point, the same safe fallback this checker already takes whenever
// it can't prove something -- so a later, real mutation of v1 is not
// (yet) caught through p specifically. No diagnostic is expected.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

void f (bool cond)
{
  std::vector<int> v1 { 1 };
  std::vector<int> v2 { 2 };
  int *q;
  if (cond)
    q = v1.data ();
  else
    q = v2.data ();
  int *p = q;
  (void) p;
}
