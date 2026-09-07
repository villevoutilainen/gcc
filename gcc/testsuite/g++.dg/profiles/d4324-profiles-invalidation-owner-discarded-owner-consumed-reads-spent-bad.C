// P3446R0/P4296R0 Invalidation profile: a DISCARDED std::owner_consumed
// call is not itself a consuming event (its own result must be used
// to count as a hand-off) -- but passing an already-spent value to it
// still reads that value, so it is caught by the same read-after-
// consumption check as any other read, not silently ignored just
// because the callee happens to be the escape hatch itself.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "utility")]];

#include <utility>

void f ([[owner]] int *p)
{
  delete p;
  std::owner_consumed (p); // { dg-error "is read here, after already being consumed" }
}
