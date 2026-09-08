// P3446R0/P4296R0 Invalidation profile: the motivating case,
// end-to-end.  std::apply()'s own internal call to f is made through
// an indirect (reference-to-function) callee, invisible to this
// checker's own call-site resolution (see invalidation-profile-
// gimple.cc's own top comment) -- so f's parameters are deliberately
// plain, non-owner pointers, and f uses std::as_owner() to legally
// delete what it receives.  main's own ownership obligations for p
// and q are discharged separately, via std::owner_consumed(), at the
// point they're captured into the tuple -- before the indirect
// hand-off std::apply performs.
// { dg-do compile { target c++11 } }
// { dg-options "-std=c++20" }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "utility")]];
[[profiles::exempt(std::invalidation, angle_header: "tuple")]];

#include <tuple>
#include <utility>

int f (int *p, int *q)
{
  int r = *p + *q;
  delete std::as_owner (p);
  delete std::as_owner (q);
  return r;
}

int use ()
{
  int *p [[owner]] = new int (42);
  int *q [[owner]] = new int (43);
  std::tuple t { std::owner_consumed (p), std::owner_consumed (q) };
  return std::apply (f, t);
}
