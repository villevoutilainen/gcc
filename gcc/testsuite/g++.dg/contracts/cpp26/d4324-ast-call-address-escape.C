// D4324: in the AST engine's basic mode (control objects, no
// -fcontract-conveyor-proofs), a call that is passed a local pointer's
// address through a parameter that can write it ends the pointer's
// is_object_address fact, as the GIMPLE engine does: 'set (&p)' may
// store anything in P.  Through a pointer-to-const or a const reference
// the callee is still given P's value, a route to the object, which it
// may end (the lifetime rule), so the fact ends there too.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=ast" }

#include <contracts>
#define TRUST(p) contract_assert<std::contracts::never_proven_conveyor_v> (std::is_object_address (p))

struct V
{
  int n;
  int size () const pre<std::contracts::conveyor_assert_v> (std::is_object_address (this))
  { return n; }
};
void set (V **);
void cset (V *const *);
void cref (V *const &);

int esc (V *p) { TRUST (p); set (&p); return p->size (); }	// { dg-error "is_object_address" }
int esc_const_ptr (V *p) { TRUST (p); cset (&p); return p->size (); }	// { dg-error "is_object_address" }
int esc_const_ref (V *p) { TRUST (p); cref (p); return p->size (); }	// { dg-error "is_object_address" }
int no_call (V *p) { TRUST (p); return p->size (); }
