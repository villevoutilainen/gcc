// D4324: in a constructor's clone, a trust or a proven assertion naming a
// parameter or a local of the constructor is about the clone's own copy;
// a by-value parameter passed by invisible reference is ended by a call
// given it like any other; inside a lambda, a member route through a
// capture by reference is the same member each time it is read.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>
#define ALIVE(x) contract_assert<std::contracts::never_proven_conveyor_v> (std::is_object_address (&x))
#define PROVEN(x) contract_assert<std::contracts::conveyor_assert_v> (std::is_object_address (&x)) // { dg-error "cannot prove .is_object_address." }

struct I { int* p; void cm () conveyor {} };
void touch (I&);
struct C1 { int x; C1 (I a) { touch (a); ALIVE (a); a.cm (); } };
struct C2 { int x; C2 () { I l{}; touch (l); ALIVE (l); l.cm (); } };
struct C3 { int x; C3 (I a) { touch (a); PROVEN (a); } };
struct C4 { int x; C4 () { I l{}; touch (l); PROVEN (l); } };
void use_ctors (I i) { C1 a (i); C2 b; C3 c (i); C4 d; }

struct N { N (const N&); ~N (); int v; };
bool ne (const N&, const N&);
bool eq (const N&, const N&) conveyor pre<std::contracts::conveyor_assert_v> (true);
void by_value (N a, N b) { ALIVE (a); ALIVE (b); ne (a, b); eq (a, b); } // { dg-error "cannot prove .is_object_address." }

struct M { int a; int get () const conveyor { return a; } };
struct Z { M m; };
bool use_member (const M&);
bool lambda (const Z& z)
{
  return [&] { ALIVE (z); use_member (z.m); return z.m.get () == 0; } (); // { dg-error "cannot prove .is_object_address." }
}
