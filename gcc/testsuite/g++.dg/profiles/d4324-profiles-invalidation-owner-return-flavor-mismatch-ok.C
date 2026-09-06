// P3446R0/P4296R0 Invalidation profile, flavor-consistency layer: an
// ordinary, unmarked function returning an ordinary, unmarked pointer
// -- baseline, no attributes involved at all.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

int* f (int *q) { return q; }
