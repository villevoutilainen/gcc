// P3446R0/P4296R0 Invalidation profile: a rebinding ends the old
// binding only on its own path; a mutation of what the new binding (or
// the old one, on another path) refers to is still caught.
// { dg-do compile { target c++17 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "list")]];

#include <list>

int moved_then_mutated (bool b)
{
  std::list<int> v{1}, u{2};
  auto it = v.begin ();
  if (b)
    {
      it = u.begin ();
      u.push_back (7);
    }
  return *it;	// { dg-error "potentially invalidated" }
}

int rebind_then_mutate (std::list<int> &v, bool b)
{
  auto it = v.begin ();
  if (b)
    {
      it = v.begin ();
      v.push_back (7);
    }
  return *it;	// { dg-error "potentially invalidated" }
}

int rebind_one_arm_then_merge (std::list<int> &v, bool b)
{
  auto it = v.begin ();
  if (b)
    it = v.begin ();
  v.push_back (7);
  return *it;	// { dg-error "potentially invalidated" }
}
