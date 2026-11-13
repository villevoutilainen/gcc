// D4324 (P4445 2.2.2.5): an access to a member of a union that isn't
// provably its active member is undefined behavior, and rejected in a
// conveyor function.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

using namespace std::contracts;

union U { int i; float f; };

float param (U u) conveyor { return u.f; } // { dg-error "not provably to its active member" }
float other () conveyor { U u{.i = 1}; return u.f; } // { dg-error "not provably to its active member" }
int uninitialized () conveyor { U u; return u.i; } // { dg-error "not provably to its active member|not explicitly initialized" }
int incremented () conveyor { U u{.f = 1}; ++u.i; return 0; } // { dg-error "not provably to its active member|overflow" }
int switched () conveyor { U u{.f = 1}; U v{.i = 1}; v = u; return v.i; } // { dg-error "not provably to its active member" }
// Not on every path.
int branches (int k) conveyor { U u{}; if (k) u.i = 1; else u.f = 2; return u.i; } // { dg-error "not provably to its active member" }
int loop () conveyor { U u{.i = 0}; for (int j = 0; j < 3; ++j) u.f = 1; return u.i; } // { dg-error "not provably to its active member" }
// A pointer to a member another write ended.
float pointer () conveyor { U u{.f = 1}; float *p = &u.f; u.i = 2; return *p; } // { dg-error "not provably valid" }
// A call given a route to the union may switch its member.
void set_i (U &u) conveyor { u.i = 1; }
float call () conveyor { U u{.f = 1}; set_i (u); return u.f; } // { dg-error "not provably to its active member" }
struct S { int x; int y; };
union W { int i; S s; };
int chain () conveyor { W w{.s = {1, 2}}; w.i = 3; return w.s.x; } // { dg-error "not provably to its active member" }
// Past the common initial sequence.
struct A { int tag; int a; };
struct B { int tag; float b; };
union V { A a; B b; };
float common_initial () conveyor { V v{.a = {1, 2}}; return v.b.b; } // { dg-error "not provably to its active member" }
// The precondition's member, not known active at the call.
float get (const U &u) conveyor
  pre<conveyor_assert_v> (std::is_object_address (&u.f))
{ return u.f; }
float caller () conveyor { U u{.i = 1}; return get (u); } // { dg-error "cannot prove 'is_object_address'" }
float through (const U &x) conveyor { return get (x); } // { dg-error "cannot prove 'is_object_address'" }
struct H
{
  U u;
  float get () const conveyor { return u.f; } // { dg-error "not provably to its active member" }
};
// A tag checked, but no trust.
struct T { int tag; union { int i; float f; }; };
float tagged (const T &t) conveyor
{ if (t.tag == 1) return t.f; return 0; } // { dg-error "not provably to its active member" }
float tagged_assert (const T &t) conveyor
{ if (t.tag == 1) { contract_assert<conveyor_assert_v> (t.f > 0); return 0; } return 0; } // { dg-error "not provably to its active member" }
