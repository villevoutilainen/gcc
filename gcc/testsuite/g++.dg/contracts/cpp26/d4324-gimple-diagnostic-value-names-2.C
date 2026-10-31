// D4324: more values the GIMPLE engine names as the source spells them
// rather than as an anonymous temporary ('<unknown>'): a call's result,
// a placement new's pointer, a '?:' merge, an element reached by
// pointer arithmetic, a load through a field, and a predicate
// function's own parameters (a postcondition's result among them), and
// the value a reference argument reads, as the object it binds to.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs -fcontract-analysis-engine=gimple" }

#include <contracts>
#include <new>
using std::contracts::conveyor_assert_v;

int pos (int x) conveyor pre<conveyor_assert_v> (x > 0);
int neg () conveyor post<conveyor_assert_v> (r: r < 0);
void call_result () conveyor { pos (neg ()); } // { dg-error "argument .neg\\(\\). provably violates" }
// { dg-message ".neg\\(\\). is established" "" { target *-*-* } .-1 }

int post_result (int a) post<conveyor_assert_v> (r: pos (r) > 0) { return a; } // { dg-warning "cannot verify that .r. satisfies" }
// { dg-warning "cannot verify postcondition" "" { target *-*-* } .-1 }

struct P { int v; P (int n) pre<std::contracts::proven_conveyor_v> (n >= 0) : v (n) {} };
void placement (void *mem) { new (mem) P (1); } // { dg-error "is_object_address. for .mem." }

struct T { int v; };
int use (T &x) conveyor { return x.v; }
int ternary (bool c, T *p) conveyor { T a{1}; return use (c ? a : *p); } // { dg-error "is_object_address. for .\\*\\(c \\? \\(& a\\) : p\\)." }

struct Outer { T t; };
int slot (Outer *st, unsigned i) conveyor { return use (st[i].t); } // { dg-error "is_object_address. for .st\\\[i\\\].Outer::t." }
// { dg-error "pointer dereference of .& st\\\[i\\\]." "" { target *-*-* } .-1 }

T &id (T &x) { return x; }
int ref_result (T &a) { return use (id (a)); } // { dg-error "is_object_address. for .id\\(a\\)." }

void byref (int &x) conveyor pre<conveyor_assert_v> (x < 100);
void bound_constant () conveyor { int local = 200; byref (local); } // { dg-error "argument .local. provably violates" }
// { dg-message ".local. is established exactly 200" "" { target *-*-* } .-1 }
void bound_copy (int k) conveyor { int l = k; byref (l); } // { dg-warning "cannot verify that .l. satisfies" }

// With several arguments of the same value, the one the precondition
// is about.
void two (int &x, int &y) conveyor pre<conveyor_assert_v> (y < 100);
void same_value () conveyor { int l = 200, m = 200; two (l, m); } // { dg-error "argument .m. provably violates" }
// { dg-message ".m. is established exactly 200" "" { target *-*-* } .-1 }
void mixed (int v, int &x) conveyor pre<conveyor_assert_v> (v < 100);
void same_literal () conveyor { int l = 200; mixed (200, l); } // { dg-error "argument .200. provably violates" }
// { dg-message ".200. is established exactly 200" "" { target *-*-* } .-1 }

// { dg-prune-output "no fact relating this value" }
