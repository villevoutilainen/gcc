// P3446R0/P4296R0 Invalidation profile: the same binding std::
// as_owner(p) establishes (see the companion -ok test) is a REAL
// obligation -- never consumed here, so it must still be flagged,
// exactly like any other fresh source.
// { dg-do compile { target c++11 } }
// { dg-options "-std=c++20" }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "utility")]];

#include <utility>

void f (int *p)
{
  int *op [[owner]] = std::as_owner (p); // { dg-error "never deleted or passed on" }
  (void) op;
}
