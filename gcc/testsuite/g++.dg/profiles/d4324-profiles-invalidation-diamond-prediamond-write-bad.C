// P3446R0/P4296R0 Invalidation profile: same diamond shape as
// d4324-profiles-invalidation-diamond-rebind-bad.C, but with a write
// to the tracked pointer ALSO occurring before the branch -- confirmed
// this checker's former dominance-only technique did not fall back to
// that stale, pre-diamond write either (it simply found nothing at
// all, from either arm); this variant exists to keep that specific,
// previously-confirmed shape under direct regression coverage.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

void f (bool cond)
{
  std::vector<int> v1 { 1, 2, 3 };
  std::vector<int> v2 { 4, 5, 6 };
  int *p = v1.data (); // pre-diamond binding: p -> v1
  if (cond)
    {
      v1.push_back (99); // mutate v1 (p's pre-diamond binding) -- moot, p is about to be rebound
      p = v1.data (); // rebind p -> v1, AFTER this arm's own mutation -- safe
    }
  else
    {
      p = v2.data (); // rebind p -> v2
      v2.push_back (99); // mutate v2 AFTER binding -- unsafe arm
    }
  int x = *p; // { dg-error "potentially invalidated" }
  (void) x;
}
