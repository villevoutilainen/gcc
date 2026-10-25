// P3446R0/P4296R0 Invalidation profile: a value bound to a container
// that is a member -- of '*this', of a reference parameter's object, of
// a local -- is invalidated by a mutation of that member, of the whole
// object, or of something that may be it.  Only a container that is a
// declaration of its own was tracked, so none of these was flagged.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "list")]];

#include <list>

struct S
{
  std::list<int> m, n;
  void helper ();
  int same_member ()
  {
    auto it = m.begin ();
    m.push_back (2);
    return *it;	// { dg-error "potentially invalidated" }
  }
  int whole_object ()
  {
    auto it = m.begin ();
    helper ();
    return *it;	// { dg-error "potentially invalidated" }
  }
  int out_of_class ();
};

int S::out_of_class ()
{
  auto it = m.begin ();
  m.push_back (2);
  return *it;	// { dg-error "potentially invalidated" }
}

int param_member (S &s)
{
  auto it = s.m.begin ();
  s.m.push_back (1);
  return *it;	// { dg-error "potentially invalidated" }
}

int local_member ()
{
  S s;
  auto it = s.m.begin ();
  s.m.push_back (1);
  return *it;	// { dg-error "potentially invalidated" }
}

int may_alias (S &s, std::list<int> &v)
{
  auto it = s.m.begin ();
  v.push_back (1);
  return *it;	// { dg-error "potentially invalidated" }
}
