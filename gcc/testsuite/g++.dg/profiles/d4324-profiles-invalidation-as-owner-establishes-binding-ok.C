// P3446R0/P4296R0 Invalidation profile: capturing std::as_owner(p)
// into an owner-declared local establishes a tracked binding (S8.1
// rule 1, same as 'new'/an owner-returning call) -- properly
// consumed here, so no diagnostic.
// { dg-do compile { target c++11 } }
// { dg-options "-std=c++20" }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "utility")]];

#include <utility>

void f (int *p)
{
  int *op [[owner]] = std::as_owner (p);
  delete op;
}
