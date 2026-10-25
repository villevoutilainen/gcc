// P3446R0/P4296R0 Invalidation profile: storing a static's address
// through a helper-returned pointer into the returned object is fine.
// { dg-do compile { target c++17 } }

[[profiles::enforce(std::invalidation)]];

struct P { int *p; P () : p (nullptr) {} P (const P &) = default; ~P () {} };
struct Q { int *p; };
int **slot_of (P &t);
int **slot_of_q (Q &t);
static int g;

P retslot () { P t; int **q = slot_of (t); *q = &g; return t; }
Q named () { Q t{}; int **q = slot_of_q (t); *q = &g; return t; }
