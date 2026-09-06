// P3446R0/P4296R0 Invalidation profile: std::owner_consumed() lets an
// [[owner]] pointer be handed off to something this checker can't see
// into (a std::unique_ptr's own constructor, here) without being
// flagged as a leak.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "memory")]];
[[profiles::exempt(std::invalidation, angle_header: "utility")]];

#include <memory>
#include <utility>

void f ([[owner]] int *p)
{
  auto up = std::unique_ptr<int> (std::owner_consumed (p));
}
