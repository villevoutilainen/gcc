// P3446R0/P4296R0 Invalidation profile, flavor-consistency layer: a
// function declared [[owner]] on its own return must only ever return
// an owner-flavored value.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

[[owner]] int* f (int *q) { return q; } // { dg-error "returning a pointer not marked" }
