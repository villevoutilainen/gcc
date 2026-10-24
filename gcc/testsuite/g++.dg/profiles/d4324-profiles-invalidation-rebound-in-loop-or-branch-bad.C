// P3446R0/P4296R0 Invalidation profile: the counterparts of
// d4324-profiles-invalidation-rebound-in-loop-or-branch-ok.C where the
// value used is not bound again after the mutation.  '++it' changes
// the value it has rather than binding a new one, and is itself the
// first use of the invalidated value.
// { dg-do compile { target c++17 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "list")]];

#include <list>

int
incremented (std::list<int> &v)
{
  auto it = v.begin ();
  v.push_back (1);
  ++it;				// { dg-error "potentially invalidated" }
  return *it;
}

int
bound_before_loop (std::list<int> &v, int n)
{
  int s = 0;
  auto it = v.begin ();
  for (int i = 0; i < n; ++i)
    {
      s += *it;			// { dg-error "potentially invalidated" }
      v.push_back (i);
    }
  return s;
}

int
mutated_after_branch (std::list<int> &v, bool b)
{
  auto it = v.begin ();
  if (b)
    it = v.begin ();
  v.push_back (7);
  return *it;			// { dg-error "potentially invalidated" }
}
