// P3446R0/P4296R0 Invalidation profile: std::as_owner() lets a
// function that only receives PLAIN, non-owner-marked pointers (the
// shape a function must have to remain callable indirectly, e.g.
// through a forwarding wrapper this checker cannot resolve a callee
// through -- see invalidation-profile-gimple.cc's own top comment)
// still legally delete what it receives.  The caller's own ownership
// obligation is discharged separately, via std::owner_consumed(),
// before the indirect hand-off.
// { dg-do compile { target c++11 } }
// { dg-options "-std=c++20" }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "utility")]];

#include <utility>

int f (int *p, int *q)
{
  int r = *p + *q;
  delete std::as_owner (p);
  delete std::as_owner (q);
  return r;
}

template<class F>
int call_it (F &&fn, int *a, int *b)
{
  return fn (a, b);
}

int use ()
{
  int *p [[owner]] = new int (42);
  int *q [[owner]] = new int (43);
  return call_it (f, std::owner_consumed (p), std::owner_consumed (q));
}
