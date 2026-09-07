// P3446R0/P4296R0 Invalidation profile: reading an [[owner]] pointer
// after handing it off via std::owner_consumed() (genuinely captured,
// so a real consuming event) -- the same "no longer safe to read"
// reasoning as delete or an ordinary owner-sink applies here too.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "memory")]];
[[profiles::exempt(std::invalidation, angle_header: "utility")]];

#include <memory>
#include <utility>

void f ([[owner]] int *p)
{
  auto up = std::unique_ptr<int> (std::owner_consumed (p));
  int x = *p; // { dg-error "is read here, after already being consumed" }
  (void) x;
}
