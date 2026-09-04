// P3446R0's Invalidation profile: [[owner]] is an alternate spelling
// for [[owning_ptr]] (Stroustrup's own CppCon 2026 "Profiles" talk,
// slides 50-53), not a rename -- both must independently satisfy the
// "delete requires an owning pointer" Negative Baseline check.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void f ([[owner]] int *p)
{
  delete p;
}
