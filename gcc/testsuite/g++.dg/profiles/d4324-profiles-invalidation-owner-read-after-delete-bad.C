// P3446R0/P4296R0 Invalidation profile: reading an [[owner]] pointer
// after it has been deleted -- not a second consuming event, an
// ordinary dereference -- was a confirmed, real false negative before
// this leak point existed: compiled with zero diagnostics.  The
// object may have been destroyed by the delete; reading it afterward
// is unsafe even though nothing consumes it a second time.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void f ([[owner]] int *p)
{
  delete p;
  int x = *p; // { dg-error "is read here, after already being consumed" }
  (void) x;
}
