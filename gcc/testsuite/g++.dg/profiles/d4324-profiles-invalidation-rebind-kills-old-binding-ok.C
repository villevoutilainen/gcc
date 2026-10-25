// P3446R0/P4296R0 Invalidation profile: once 'it' is bound again on a
// path, a later mutation on that path is no longer charged to what it
// was bound to before.  Rebinding 'it' to u and then mutating v was
// rejected as a use of the stale v binding.
// { dg-do compile { target c++17 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "list")]];

#include <list>

int moved (bool b)
{
  std::list<int> v{1}, u{2};
  auto it = v.begin ();
  if (b)
    {
      it = u.begin ();
      v.push_back (7);
    }
  return *it;
}

int loop_rebind (std::list<int> &v, int n)
{
  int s = 0;
  auto it = v.begin ();
  for (int i = 0; i < n; ++i)
    {
      it = v.begin ();
      s += *it;
      v.push_back (i);
    }
  return s;
}
