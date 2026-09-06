// P3446R0/P4296R0 Invalidation profile: an [[owner]] parameter deleted
// on its only path is fully consumed.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void f ([[owner]] int *p) { delete p; }
