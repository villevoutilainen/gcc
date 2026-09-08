// P3446R0/P4296R0 Invalidation profile: wrapping an ALREADY genuinely
// owner-declared pointer's delete in std::as_owner() must not
// regress -- the GIMPLE-level resolver (ip_owner_fresh_source_call_p)
// must still recognize this as consuming p's own tracked binding,
// not falsely report it as never deleted.
// { dg-do compile { target c++11 } }
// { dg-options "-std=c++20" }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "utility")]];

#include <utility>

void f ()
{
  int *p [[owner]] = new int (5);
  delete std::as_owner (p);
}
