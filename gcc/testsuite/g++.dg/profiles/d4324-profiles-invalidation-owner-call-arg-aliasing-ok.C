// P3446R0/P4296R0 Invalidation profile: two DISTINCT [[owner]]
// pointers passed to two owner-accepting parameters is fine -- no
// aliasing, two genuinely independent ownership transfers.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void f (int *p [[owner]], int *q [[owner]]);

void g ([[owner]] int *a, [[owner]] int *b) { f (a, b); }

// A null argument at multiple owner-accepting positions is exempt --
// null represents no object at all, so aliasing is moot.
void h () { f (nullptr, nullptr); }
