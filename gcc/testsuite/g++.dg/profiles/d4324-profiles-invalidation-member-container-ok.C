// P3446R0/P4296R0 Invalidation profile: mutating a different member, or
// a fresh local, leaves a value bound to a member container valid.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "list")]];

#include <list>

struct S
{
  std::list<int> m, n;
  int other_member ()
  {
    auto it = m.begin ();
    n.push_back (2);
    return *it;
  }
  int fresh_local ()
  {
    std::list<int> u{1};
    auto it = m.begin ();
    u.push_back (1);
    return *it;
  }
};

int param_other_member (S &s)
{
  auto it = s.m.begin ();
  s.n.push_back (1);
  return *it;
}
