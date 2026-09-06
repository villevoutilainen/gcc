// P3446R0/P4296R0 Invalidation profile: std::owner_consumed(x) is
// itself just one more consuming event -- using it twice for the same
// x (here, each use genuinely captured into a real unique_ptr, so
// this isn't the discarded-result case) is still a double-consumption.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "memory")]];
[[profiles::exempt(std::invalidation, angle_header: "utility")]];

#include <memory>
#include <utility>

void caller ([[owner]] int *x)
{
  auto up1 = std::unique_ptr<int> (std::owner_consumed (x));
  auto up2 = std::unique_ptr<int> (std::owner_consumed (x)); // { dg-error "consumed again here" }
}
