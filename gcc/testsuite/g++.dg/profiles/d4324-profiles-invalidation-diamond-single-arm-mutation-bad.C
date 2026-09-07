// P3446R0/P4296R0 Invalidation profile: the SAME binding (not a
// diamond of different bindings) reaching a use after a mutation
// present on only ONE arm of a branch -- a second, independent
// dominance-based defect this checker used to have (dominated_by_p, a
// UNIVERSAL "happens on every path" test, answering what should be an
// EXISTENTIAL "could this happen on some path" question): a mutation
// on just one arm never dominates the merge point, so it used to be
// silently treated as inapplicable, even though the true-branch path
// genuinely dereferences an invalidated pointer.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

void f (bool cond)
{
  std::vector<int> v { 1, 2, 3 };
  int *p = v.data ();
  if (cond)
    v.push_back (99); // invalidates p, but only on this one arm
  int x = *p; // { dg-error "potentially invalidated" }
  (void) x;
}
