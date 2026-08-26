// D4324/P2680, soundness companion to the -ok.C case: an array-slot
// identity fact established for '&st[current]' must NOT survive a
// later reassignment of CURRENT -- unlike a FIELD_DECL (immutable, so
// field_object_identity_key's own second key component never needs
// this check), an array-slot key's own second component is a decl
// identity too, and the invalidation sweep must catch a reassignment
// of either key component, not just the array pointer's own. Found via
// direct testing during this feature's own development: without the
// both-key-components check in field_object_predicate_invalidate_all/
// field_object_address_invalidate_all, this compiled clean when it
// must not.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct Inner { int v; };
struct Outer { Inner tickets; };

int use_val_const (const Inner& x) conveyor { return x.v; }

int
reject_after_index_reassignment (Outer* state, unsigned current) conveyor
{
  Outer* const st = state;
  contract_assert<std::contracts::never_proven_conveyor_v>
    (std::is_object_address (&st[current]));
  current = current + 1; // reassigned -- the established fact must not survive
  return use_val_const (st[current].tickets); // { dg-error "cannot prove .is_object_address." }
                                                // { dg-error "pointer dereference of" "" { target *-*-* } .-1 }
}

int main () { return 0; }
