// D4324/P2680, Increment 2 of the pointer-indexing follow-on: an
// assertable array-slot identity for 'ptr[dynamic_index]', modeled
// directly on the existing field_object_identity_key mechanism
// ('&h->f') -- oa_array_object_identity/array_object_identity_key.
// Unlike Increment 1's oa_get_range-based composition (only usable for
// a pointer with real, traceable named-array provenance), this lets a
// library author assert is_object_address(&ptr[index]) into existence
// for an opaque pointer with no such provenance (e.g. std::barrier's
// own heap-allocated __state), and have it compose through a further
// field access ('.tickets') the same way an ordinary '&h->f' fact
// already does. Mirrors the exact shape std::barrier's own
// __tree_barrier_base::_M_arrive needs.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct Inner { int v; };
struct Outer { Inner tickets; };

int use_val_const (const Inner& x) conveyor { return x.v; }

// STATE has no traceable named-array provenance from inside this
// function -- an ordinary pointer parameter, exactly like std::
// barrier's own __state (loaded from an atomic, or here, simply
// received as an opaque pointer).
int
arrive (Outer* state, unsigned current) conveyor
{
  Outer* const st = state;
  contract_assert<std::contracts::never_proven_conveyor_v>
    (std::is_object_address (&st[current]));
  return use_val_const (st[current].tickets);
}

int
main ()
{
  Outer arr[4] = { {{1}}, {{2}}, {{3}}, {{4}} };
  return arrive (arr, 2) - 3;
}
