// P3446R0/P4296R0 Invalidation profile, Phase 7a Negative Baseline
// (S7.2, [ub:class.dtor.no.longer.exists] et al.): a genuinely
// explicit destructor call ("obj.~X()") is rejected once the
// std::invalidation profile is enforced -- via a reference parameter
// specifically, so this test cannot be confused with the separate
// pointer-dereference rule (a pointer parameter would trip both
// rules on "p->~X()").
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

struct S { ~S() {} };

void f (S &s)
{
  s.~S(); // { dg-error "explicit destructor call not permitted" }
}
