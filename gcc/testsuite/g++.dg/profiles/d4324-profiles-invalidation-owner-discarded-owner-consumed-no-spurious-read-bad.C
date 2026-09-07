// P3446R0/P4296R0 Invalidation profile: a DISCARDED std::owner_consumed
// call on a value that has NOT yet been consumed must not spuriously
// trigger the new read-after-consumption check -- only the pre-
// existing "never deleted or passed on" leak diagnostic is expected,
// exactly once, confirming the new check stays silent until the
// value is genuinely already spent.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "utility")]];

#include <utility>

void f ([[owner]] int *p) // { dg-error "never deleted or passed on" }
{
  std::owner_consumed (p);
}
