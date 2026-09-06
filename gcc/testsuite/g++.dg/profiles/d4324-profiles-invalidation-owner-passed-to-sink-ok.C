// P3446R0/P4296R0 Invalidation profile: an [[owner]] parameter handed
// off to another owner-accepting sink parameter is consumed.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void sink ([[owner]] int *q);

void f ([[owner]] int *p) { sink (p); }
