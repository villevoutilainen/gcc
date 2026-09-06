// P3446R0/P4296R0 Invalidation profile: reassigning an [[owner]]
// parameter before consuming its original value is a leak, even
// though the FINAL value is properly deleted -- necessary for
// soundness, not optional (see ip_check_owner_binding's own comment,
// invalidation-profile-gimple.cc): a checker that only looked at the
// final value of p would miss that whatever p originally held was
// silently discarded here.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

[[owner]] int* g ();

void f ([[owner]] int *p)
{
  p = g (); // { dg-error "reassigned here, discarding a not-yet-consumed" }
  delete p;
}
