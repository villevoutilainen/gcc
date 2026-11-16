// D4324: a pointer stored where a later call can reach it -- a global, a
// member -- lets that call end the object, though given no route to it
// (P4445 caller6).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=ast" }

#include <contracts>
namespace sc = std::contracts;

struct S { int v; int get () const conveyor { return v; } };
S *g;
void release ();		// opaque: may end what g points to
void keep (S *);		// opaque: may keep its argument
int use (const S &s) conveyor { return s.v; }
struct H { S *p; };

// The function stores the pointer where a later call can reach it: that
// call may end the object, though given no route to it.
int stored_global (S *p) pre<sc::proven_conveyor_v> (std::is_object_address (p))
{ g = p; release (); return use (*p); }	// { dg-error "cannot prove .is_object_address." }
int stored_member (S *p, H &h) pre<sc::proven_conveyor_v> (std::is_object_address (p))
{ h.p = p; release (); return use (*p); }	// { dg-error "cannot prove .is_object_address." }

// No call since the store, or only a conveyor one: still an object.
int stored_no_call (S *p) pre<sc::proven_conveyor_v> (std::is_object_address (p))
{ g = p; return use (*p); }
int stored_conveyor_call (S *p) pre<sc::proven_conveyor_v> (std::is_object_address (p))
{ g = p; (void) p->get (); return use (*p); }
// Into a part of a local: not out of the function.
int stored_local (S *p) pre<sc::proven_conveyor_v> (std::is_object_address (p))
{ H h; h.p = p; release (); return use (*p); }

// Documented limits: an alias the caller made, which the function can't
// see; and a pointer handed to a call, whose facts end at that call, then
// trusted again -- the trust vouches for it after later calls too.
int callers_alias (S *p) pre<sc::proven_conveyor_v> (std::is_object_address (p))
{ release (); return use (*p); }
int handed_then_trusted (S *p) pre<sc::proven_conveyor_v> (std::is_object_address (p))
{ keep (p); contract_assert<sc::never_proven_conveyor_v> (std::is_object_address (p)); release (); return use (*p); }
