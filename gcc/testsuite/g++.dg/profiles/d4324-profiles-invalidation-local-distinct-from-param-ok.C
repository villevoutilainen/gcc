// P3446R0/P4296R0 Invalidation profile: an automatic local is created
// after every argument, so no reference parameter can refer to it.
// Mutating the parameter's container doesn't invalidate a value bound
// to the local's, nor the other way around; both were rejected when
// the two containers had the same type.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "list")]];

#include <list>

int rebound_to_local (std::list<int> &v, bool b)
{
  std::list<int> u{1};
  auto it = v.begin ();
  if (b)
    {
      it = u.begin ();
      v.push_back (7);
    }
  return *it;
}

int local_mutated (std::list<int> &v)
{
  std::list<int> u{1};
  auto it = v.begin ();
  u.push_back (7);
  return *it;
}

int param_mutated (std::list<int> &v)
{
  std::list<int> u{1};
  auto it = u.begin ();
  v.push_back (7);
  return *it;
}
