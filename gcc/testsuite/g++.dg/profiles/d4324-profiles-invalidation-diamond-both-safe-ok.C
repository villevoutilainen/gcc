// P3446R0/P4296R0 Invalidation profile: same diamond shape as the
// -bad.C variants, but BOTH branches are genuinely safe (each mutates
// its own container before binding the pointer to it, never after) --
// must stay accepted. Guards against the fix for the diamond false
// negative accidentally becoming an over-eager false positive on a
// diamond that never actually reaches this checker's own merge-point
// dataflow with a real problem.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

void f (bool cond)
{
  std::vector<int> v1 { 1, 2, 3 };
  std::vector<int> v2 { 4, 5, 6 };
  int *p;
  if (cond)
    {
      v1.push_back (99);
      p = v1.data ();
    }
  else
    {
      v2.push_back (99);
      p = v2.data ();
    }
  int x = *p;
  (void) x;
}
