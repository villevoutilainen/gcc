// D4324: a fact from a branch on a predicate call stands for the
// predicate's next call only if that call gives the same result.  A strict
// predicate can't change what its next call returns: each way it could
// write the state it reads is rejected (P4445 BUG-21, with the BUG-15
// rules).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=ast" }

#include <contracts>

// A non-const conveyor member writing a member: a strict predicate can't
// call it on an object it doesn't own.
struct C {
  int n = 0;
  bool flip () conveyor { n = !n; return n != 0; }
};
void need1 (C &c) pre<std::contracts::proven_conveyor_v> (c.flip ()); // { dg-error "object .c. is not owned by the calling function" }
void use1 () { C c; if (c.flip ()) need1 (c); }

// A conveyor function writing through a reference parameter: nor can it
// pass an object it doesn't own to one.
bool step (int &r) conveyor { r = r < 10 ? r + 1 : 0; return r == 1; }
void need2 (int &r) pre<std::contracts::proven_conveyor_v> (step (r)); // { dg-error "is not owned by the calling function, so it may not be passed" }
void use2 () { int i = 0; if (step (i)) need2 (i); }

// A const member writing through a pointer member, with no mutable member:
// the write itself is rejected.
struct D {
  int *q;
  bool bump () const conveyor { *q = *q < 10 ? *q + 1 : 0; return *q == 1; } // { dg-error "write through pointer member .D::q. of a const object" }
};

// The same through a pointer-to-const parameter whose pointee holds a
// pointer.
struct E { int *q; };
bool bump2 (const E *e) conveyor
{ *e->q = *e->q < 10 ? *e->q + 1 : 0; return *e->q == 1; } // { dg-error "write through pointer member .E::q. of a const object" }
