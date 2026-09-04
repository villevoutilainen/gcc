// P3446R0/P4296R0 Invalidation profile, Phase 7a Negative Baseline
// (S7.2, [ub:expr.delete.mismatch]): 'delete' of a pointer not marked
// [[owning_ptr]] is rejected once the std::invalidation profile is
// enforced.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void f (int *p)
{
  delete p; // { dg-error "not marked" }
}
