// D4324: soundness regression for the shared predicate-fact substrate
// under proven_symbolic forcing, with no proofs flag anywhere.
// open_it's postcondition establishes is_opened (f); reassigning f's
// own value (not just a pointer repoint) must invalidate that fact --
// otherwise use_it's own precondition below would be wrongly accepted
// as "proven" for a fresh, unopened file. Confirmed (not assumed) that
// this stays correctly rejected both before and after widening the 5
// bookkeeping gates (oa_walk_stmt's "Shared-substrate invalidation
// rule 1" already ran whenever the call-obligation-discharge family's
// own, separately-forced consultation needed it); kept as a permanent
// regression test guarding the same class of gap the other new tests
// in this group close.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

struct file { bool opened = false; };
bool is_opened (const file&) symbolic;

void open_it (file& f) post<sc::proven_symbolic_v>(is_opened (f))
{
  f.opened = true;
}

void use_it (const file& f) pre<sc::proven_symbolic_v>(is_opened (f))
{
}

void bad ()
{
  file f;
  open_it (f);
  f = file (); // reassigns f's own value -- must invalidate is_opened(f)
  use_it (f); // { dg-error "cannot prove" }
}

int main () { bad (); return 0; }
