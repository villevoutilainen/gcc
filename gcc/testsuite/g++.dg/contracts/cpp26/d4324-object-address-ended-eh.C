// D4324: a call that throws is taken not to have ended the objects it
// was given; the cleanups run on an exception path after it are neither
// checked nor end anything, as the AST engine (which has no exception
// paths) sees.  A call that returned may still have ended them.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs -fcontract-analysis-engine=ast" }

#include <contracts>
#define ALIVE(x) contract_assert<std::contracts::never_proven_conveyor_v> (std::is_object_address (&x))

struct A { A (); ~A () conveyor {} };
struct G { A* a; ~G (); };
int* take (A&);
G mk (A&);
void mayth ();

int* thrown () { A a; int* p = take (a); ALIVE (a); return p; }
void guard () { A a; ALIVE (a); { G g = mk (a); ALIVE (a); mayth (); } ALIVE (a); }
void returned () { A a; take (a); } // { dg-error "cannot prove .is_object_address." }
