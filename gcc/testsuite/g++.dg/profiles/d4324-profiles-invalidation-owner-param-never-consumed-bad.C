// P3446R0/P4296R0 Invalidation profile: an [[owner]] parameter that is
// never deleted or handed off is a leak.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void f ([[owner]] int *p) {} // { dg-error "never deleted or passed on" }
