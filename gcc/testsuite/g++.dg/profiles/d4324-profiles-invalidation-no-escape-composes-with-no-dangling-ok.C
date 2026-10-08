// P3446R0 Invalidation profile: std::no_escape() asserts only that the
// reference formed to bind THIS argument isn't retained -- it does not
// also assert the wrapped value itself is safe. When the value's own
// safety isn't already established (here, get_pointer's own return
// value needs trusting, same as any opaque call's return value per
// this checker's own default-deny stance), the correct pattern is to
// name the intermediate value, trust it explicitly with
// std::no_dangling, then wrap the name with std::no_escape for the
// outer binding. std::no_dangling(get_pointer(p)) passed directly to
// push_back would NOT be enough on its own: no_dangling is by-value, so
// its own return is a fresh temporary that still needs no_escape's
// reference-binding assertion one level out.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];
[[profiles::exempt(std::invalidation, angle_header: "utility")]];

#include <vector>
#include <utility>

int* get_pointer (int* p) { return p; }

std::vector<int*> f (int* p)
{
  std::vector<int*> v;
  auto *tmp = std::no_dangling (get_pointer (p));
  v.push_back (std::no_escape (tmp));
  return v;
}
