// P3446R0/P4296R0 Invalidation profile: a local's address stored into
// a member of a class returned through a hidden return-slot pointer,
// after the object is constructed.  Only stores of the whole object
// were seen, so 'P t; t.p = &c; return t;' was accepted.  So is a call
// given the member's address (in any argument position, possibly
// through a temporary or a merge), which may store its other arguments
// there.
// { dg-do compile { target c++17 } }

[[profiles::enforce(std::invalidation)]];

struct P { int *p; P () : p (nullptr) {} P (const P &) = default; ~P () {} };
void put (int **slot, int *v);
void fill (int *v, P &t);

P member () { int c = 1; P t; t.p = &c; return t; }		// { dg-error "returning a pointer or container" }
P member_one_branch (bool b) { int c = 1; P t; if (b) t.p = &c; return t; }	// { dg-error "returning a pointer or container" }
P by_call () { int c = 1; P t; put (&t.p, &c); return t; }	// { dg-error "returning a pointer or container" }
P by_call_second () { int c = 1; P t; fill (&c, t); return t; }	// { dg-error "returning a pointer or container" }
P by_call_merged (bool b)
{
  int c = 1;
  int *o;
  P t;
  int **q = b ? &t.p : &o;
  put (q, &c);
  return t;							// { dg-error "returning a pointer or container" }
}
