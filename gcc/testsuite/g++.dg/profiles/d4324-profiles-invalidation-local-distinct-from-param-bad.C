// P3446R0/P4296R0 Invalidation profile: two reference parameters, or a
// parameter and a static, may be the same container.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "list")]];

#include <list>

int two_params (std::list<int> &v, std::list<int> &w)
{
  auto it = v.begin ();
  w.push_back (7);
  return *it;	// { dg-error "potentially invalidated" }
}

static std::list<int> gl;

int param_and_static (std::list<int> &v)
{
  auto it = gl.begin ();
  v.push_back (7);
  return *it;	// { dg-error "potentially invalidated" }
}
