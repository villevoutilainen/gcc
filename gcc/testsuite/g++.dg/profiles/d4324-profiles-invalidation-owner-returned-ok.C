// P3446R0/P4296R0 Invalidation profile: an [[owner]] parameter handed
// back out via the function's own [[owner]]-marked return is consumed
// (ownership transferred to the caller).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

[[owner]] int* f ([[owner]] int *p) { return p; }
