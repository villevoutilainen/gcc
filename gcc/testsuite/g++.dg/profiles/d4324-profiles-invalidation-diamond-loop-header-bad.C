// P3446R0/P4296R0 Invalidation profile: the new fixed-point dataflow
// exercised across a loop header (a block with two predecessors, one
// of them its own back-edge) -- p is safe on loop entry, but the
// merge at the loop header also carries the back-edge path, on which
// a previous iteration's own push_back already invalidated it.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

void f (int n)
{
  std::vector<int> v { 1, 2, 3 };
  int *p = v.data ();
  for (int i = 0; i < n; ++i)
    {
      int x = *p; // { dg-error "potentially invalidated" }
      (void) x;
      v.push_back (i);
    }
}
