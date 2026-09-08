// P3446R0/P4296R0 Invalidation profile: std::as_owner(p) passed
// directly into an owner-accepting call-argument position satisfies
// the reverse-direction genuineness check (S8.5) -- p need not itself
// be owner-declared or trace to a 'new'-expression.
// { dg-do compile { target c++11 } }
// { dg-options "-std=c++20" }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "utility")]];

#include <utility>

void sink (int *p [[owner]])
{
  delete p;
}

void f (int *p)
{
  sink (std::as_owner (p));
}
