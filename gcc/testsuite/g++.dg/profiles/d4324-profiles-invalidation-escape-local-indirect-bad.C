// P3446R0 Invalidation profile: returning a pointer to a local
// THROUGH an intermediate local pointer variable is also flagged --
// distinct from d4324-profiles-invalidation-escape-local-bad.C's own
// direct "return &x;" case (checked at the front end, since
// gimplification erases that exact shape before any GIMPLE pass
// could see it): here the escape is only visible by tracing the
// intermediate variable's own reaching definition, which is exactly
// invalidation-profile-gimple.cc's own ip_escapes_locally_p job.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

int *f ()
{
  int x = 7;
  int *p = &x;
  return p; // { dg-error "may hold a pointer to a local" }
}
