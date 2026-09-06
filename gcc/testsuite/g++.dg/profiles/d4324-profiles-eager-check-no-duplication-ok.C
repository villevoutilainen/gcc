// P3446R0/P4222 profiles: with no front-end error anywhere in the
// translation unit, the eager per-function checking mechanism must
// not cause any diagnostic to fire twice (a regression risk from
// running both profile-checking passes directly, per function,
// rather than once via the normal end-of-compilation pipeline).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];
[[profiles::enforce(std::invalidation)]];

void f (int *p [[owner]])
{
  delete p;
}

int g ()
{
  int x [[uninit]];
  int *p [[ref_to_uninit]] = &x;
  x = 5;
  return *p;
}
