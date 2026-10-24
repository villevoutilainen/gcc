// P3446R0/P4296R0 Invalidation profile: an iterator bound again after
// the mutation that would invalidate it -- each loop iteration (a
// range-for's element, or 'auto it = v.begin ();' in the body), or on
// the branch that mutates.  A mutation since the binding counted even
// when the variable had been bound again since, so the fresh value was
// reported as invalidated.
// { dg-do compile { target c++17 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "list")]];

#include <list>

int
loop (std::list<int> &v, int n)
{
  int s = 0;
  for (int i = 0; i < n; ++i)
    {
      auto it = v.begin ();
      s += *it;
      v.push_back (i);
    }
  return s;
}

int
branch (std::list<int> &v, bool b)
{
  auto it = v.begin ();
  if (b)
    {
      v.push_back (7);
      it = v.begin ();
    }
  return *it;
}

int
range_for (std::list<int> &v)
{
  int s = 0;
  for (auto &x : v)
    s += x;
  return s;
}
