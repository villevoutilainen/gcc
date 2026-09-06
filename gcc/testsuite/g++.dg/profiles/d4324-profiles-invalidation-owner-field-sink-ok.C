// P3446R0/P4296R0 Invalidation profile: storing an [[owner]] pointer
// into an [[owner]]-marked field transfers ownership to the containing
// object -- a valid consuming event, even though this checker
// deliberately does NOT itself track that field's own eventual
// destruction (a separate, harder, whole-class-lifetime question).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

struct S { [[owner]] int *m; };

void f ([[owner]] int *p, S &s) { s.m = p; }
