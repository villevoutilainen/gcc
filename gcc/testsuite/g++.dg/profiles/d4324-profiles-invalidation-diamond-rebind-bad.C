// P3446R0/P4296R0 Invalidation profile: a diamond where each branch
// establishes a DIFFERENT binding for the same pointer, merging
// before a use -- one branch's own binding is genuinely unsafe (bound
// to a container, then that same container mutated, before the
// merge-point use). This is a real false negative this checker used
// to have: the merge-point use silently compiled clean, with no
// diagnostic at all, because the dominance-only "nearest write"
// technique this checker previously used couldn't see either arm's
// own binding from the merge point at all. Fixed by a real reaching-
// definitions dataflow.
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
      v1.push_back (99); // mutate v1 BEFORE binding p to it -- safe arm
      p = v1.data ();
    }
  else
    {
      p = v2.data (); // bind p to v2
      v2.push_back (99); // mutate v2 AFTER binding -- unsafe arm
    }
  int x = *p; // { dg-error "potentially invalidated" }
  (void) x;
}
