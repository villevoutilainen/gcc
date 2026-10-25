// P3446R0/P4296R0 Invalidation profile: a local's address stored
// through a pointer into the returned object that a helper returned
// ('int **q = slot_of (t); *q = &c;').  Only pointers made from '&t'
// itself, copies, offsets and merges were recognized as pointing into
// T, so these stores were missed, whether T lives in the hidden return
// slot or is a named local copied out.
// { dg-do compile { target c++17 } }

[[profiles::enforce(std::invalidation)]];

struct P { int *p; P () : p (nullptr) {} P (const P &) = default; ~P () {} };
struct Q { int *p; };
int **slot_of (P &t);
int **slot_of_q (Q &t);

P retslot () { int c = 1; P t; int **q = slot_of (t); *q = &c; return t; }	// { dg-error "returning a pointer or container" }
Q named () { int c = 1; Q t{}; int **q = slot_of_q (t); *q = &c; return t; }	// { dg-error "returning a pointer or container" }
P by_reference () { int c = 1; P t; int *&r = *slot_of (t); r = &c; return t; }	// { dg-error "returning a pointer or container" }
