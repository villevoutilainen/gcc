// P3446R0/P4296R0 Invalidation profile: reassigning an [[owner]]
// parameter AFTER it has already been consumed is fine -- the
// reassignment doesn't discard anything unconsumed, it discards
// nothing at all (the binding was already settled).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void f ([[owner]] int *p)
{
  delete p;
  p = nullptr;
}
