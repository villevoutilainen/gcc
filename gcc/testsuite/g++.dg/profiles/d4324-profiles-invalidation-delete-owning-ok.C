// P3446R0/P4296R0 Invalidation profile, Phase 7a Negative Baseline: a
// pointer marked [[owning_ptr]] may still be 'delete'd -- this is the
// annotation's entire purpose (P4296R0 S7.2's "purely syntactic
// change" example).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void f ([[owning_ptr]] int *p)
{
  delete p;
}
