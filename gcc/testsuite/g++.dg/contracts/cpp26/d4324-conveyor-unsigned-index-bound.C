// D4324: an unsigned index is never below zero, so an upper bound alone
// ('n < 2', from a trust, a precondition or a condition) bounds it; and
// an element of a member array of '*this' is one of its subobjects, so a
// conveyor function may return a reference to it.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs -fcontract-analysis-engine=ast" }

#include <contracts>
namespace sc = std::contracts;
#define NP sc::never_proven_conveyor_v
#define SELF pre<sc::conveyor_assert_v> (std::is_object_address (this))

struct A
{
  int e[2];
  int& by_if (unsigned n) conveyor SELF { if (n < 2) return e[n]; return e[0]; }
  int& by_trust (unsigned n) conveyor SELF { contract_assert<NP> (n < 2); return e[n]; }
  int by_value (unsigned long n) conveyor SELF { contract_assert<NP> (n < 2); return e[n]; }
  int by_pre (unsigned n) conveyor SELF pre<sc::conveyor_assert_v> (n < 2) { return e[n]; }
  int signed_unbounded (int n) conveyor SELF { contract_assert<NP> (n < 2); return e[n]; } // { dg-error "not provably in-bounds" }
  int& first () conveyor SELF { return e[0]; }
};
