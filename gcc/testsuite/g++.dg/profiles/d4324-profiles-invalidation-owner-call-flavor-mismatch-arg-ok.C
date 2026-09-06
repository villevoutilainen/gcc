// P3446R0/P4296R0 Invalidation profile, flavor-consistency layer: an
// ordinary, unmarked pointer passed to an ordinary, unmarked
// parameter -- baseline, no attributes involved at all.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void sink (int *q);

void f (int *p) { sink (p); }
