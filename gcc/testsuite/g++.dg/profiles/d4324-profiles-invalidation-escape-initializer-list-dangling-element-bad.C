// P3446R0 Invalidation profile: contrast case for the sibling
// escape-seethrough-pointer-initializer-list-ok.C -- the new ARRAY_REF
// element-write tracing in ip_collect_component_writes_before must
// still correctly find and flag a genuinely dangling element, not
// just safely trust whatever it finds. '&local' stored into the
// initializer_list's own backing array element must stay flagged,
// exactly as 'v.push_back(&local)' already does in the sibling
// d4324-profiles-invalidation-escape-explicit-local-address-bad.C.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

std::vector<int*> f ()
{
  int local = 0;
  return {&local}; // { dg-error "may hold a pointer to a local" }
}
