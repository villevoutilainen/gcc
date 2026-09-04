// P3446R0/P4296R0 Invalidation profile, Phase 7a Negative Baseline
// (S7.2's [ub:intro.object.implicit.create]/[ub:intro.object.implicit
// .pointer] discussion): a reinterpret_cast to pointer type is
// rejected once the std::invalidation profile is enforced.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void f (int *p)
{
  float *fp = reinterpret_cast<float *> (p); // { dg-error "reinterpret_cast. to pointer type not permitted" }
  (void) fp;
}
