// P3446R0/P4296R0 Invalidation profile: reading x's own name after
// its value was handed off to another, independently owner-marked
// variable y -- the exact scenario motivating this whole leak point:
// passing (or here, copying) an [[owner]] value elsewhere doesn't
// merely make it "no longer owned by x", it makes reading x unsafe,
// because whatever now owns it (y, here) may have destroyed it.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void f ([[owner]] int *x)
{
  [[owner]] int *y = x;
  int v = *x; // { dg-error "is read here, after already being consumed" }
  (void) v;
  delete y;
}
