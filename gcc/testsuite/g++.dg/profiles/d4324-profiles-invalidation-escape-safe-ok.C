// P3446R0 Invalidation profile: a function may return a pointer to a
// static/global object or a received pointer parameter -- neither
// dangles (CppCon 2026 "Profiles" talk, slide 45).  The same slide's
// third safe case, returning the result of 'new' (leak concerns
// aside), is not exercisable in this same translation unit: declaring
// operator new at all is separately, unconditionally flagged by this
// profile's own Negative Baseline (Phase 7a) regardless of this
// check, so there is no way to name it here without that unrelated
// diagnostic firing first.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

int glob = 5;

int *to_global ()
{
  return &glob;
}

int *to_static ()
{
  static int s = 0;
  return &s;
}

int *received (int *p)
{
  return p;
}
