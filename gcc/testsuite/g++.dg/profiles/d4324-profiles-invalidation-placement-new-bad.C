// P3446R0/P4296R0 Invalidation profile, Phase 7a Negative Baseline
// (S7.2, [ub:original.type.implicit.destructor] et al.): placement
// new is rejected once the std::invalidation profile is enforced.
// <new>'s own declarations are exempted (P3589 Phase 5's own
// mechanism) so only the placement-new expression itself, not <new>'s
// operator new/delete declarations, is under test here.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "new")]];

#include <new>

struct S { ~S() {} };

void f (S &s)
{
  new (&s) S (); // { dg-error "placement .new. not permitted" }
}
