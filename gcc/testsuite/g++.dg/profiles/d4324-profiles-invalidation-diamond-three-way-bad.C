// P3446R0/P4296R0 Invalidation profile: the new reaching-definitions/
// mutated-since dataflow's own fixed-point loop, exercised beyond the
// minimal two-arm case -- a three-way merge (if/else-if/else), where
// only the middle arm's own binding is unsafe.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

void f (int which)
{
  std::vector<int> v1 { 1 };
  std::vector<int> v2 { 2 };
  std::vector<int> v3 { 3 };
  int *p;
  if (which == 0)
    p = v1.data ();
  else if (which == 1)
    {
      p = v2.data ();
      v2.push_back (9); // unsafe: mutated right after binding
    }
  else
    p = v3.data ();
  int x = *p; // { dg-error "potentially invalidated" }
  (void) x;
}
