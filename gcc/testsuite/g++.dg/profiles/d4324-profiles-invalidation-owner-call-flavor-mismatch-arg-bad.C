// P3446R0/P4296R0 Invalidation profile, flavor-consistency layer: an
// [[owner]] pointer passed to a call argument whose corresponding
// parameter is NOT marked [[owner]] is a flavor mismatch, independent
// of the definite-consumption layer (which separately flags p as
// never consumed here, since sink's own parameter isn't an
// owner-accepting sink).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void sink (int *q);

void f ([[owner]] int *p) // { dg-error "never deleted or passed on" }
{
  sink (p); // { dg-error "but its parameter is not marked" }
}
