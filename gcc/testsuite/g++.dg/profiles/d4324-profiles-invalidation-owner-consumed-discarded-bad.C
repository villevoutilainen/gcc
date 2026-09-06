// P3446R0/P4296R0 Invalidation profile: a std::owner_consumed(x) call
// whose own return value is discarded (not assigned to anything, not
// passed on to anything) doesn't actually hand x off anywhere -- it's
// a no-op assertion, so x is still leaked, exactly as if the call
// weren't there at all.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "utility")]];

#include <utility>

void caller ([[owner]] int *x) // { dg-error "never deleted or passed on" }
{
  std::owner_consumed (x);
}
