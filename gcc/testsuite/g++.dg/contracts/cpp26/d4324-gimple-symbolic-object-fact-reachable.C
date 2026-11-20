// D4324: a symbolic predicate of a class taken by reference ('warm (const
// H &)') is a fact about the object, which may depend on what its pointers
// reach (a pImpl handle's cache).  A call that isn't conveyor given a copy
// of the object, or a pointer read out of it, ends the fact there, and the
// pointers it got escape: any later such call ends the fact too.  A copy
// into a local is tracked: a call given the copy ends the original's fact.
// A by-value predicate ('hot (H)') is a value fact, unaffected.  The gimple
// engine.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs -fcontract-analysis-engine=gimple" }

#include <contracts>
using namespace std::contracts;

struct Impl { int x; };
struct H { Impl *p; };
struct N { int a, b; };				// reaches nothing

bool warm (const H &) symbolic;
bool hot (H) symbolic;
bool ok (const N &) symbolic;
H make () post<never_proven_symbolic_v>(r: warm (r));
H make_hot () post<never_proven_symbolic_v>(r: hot (r));
N make_n () post<never_proven_symbolic_v>(r: ok (r));
int use (const H &h) pre<symbolic_assert_v>(warm (h));
int use_hot (H h) pre<symbolic_assert_v>(hot (h));
int use_n (const N &n) pre<symbolic_assert_v>(ok (n));
void by_val (H);
void by_val_n (N);
void by_ptr (Impl *);
void conv_by_val (H) conveyor;
void other ();
H g;

int k1 () { H a = make (); return use (a); }
int k2 () { H a = make (); other (); return use (a); }
// Given a copy, or a pointer out of it.
int k3 () { H a = make (); by_val (a); return use (a); } // { dg-warning "cannot verify" }
int k4 () { H a = make (); by_ptr (a.p); return use (a); } // { dg-warning "cannot verify" }
int k5 () { H a = make (); Impl *q = a.p; by_ptr (q); return use (a); } // { dg-warning "cannot verify" }
// Through a local copy.
int k6 () { H a = make (); H b = a; by_val (b); return use (a); } // { dg-warning "cannot verify" }
int k7 () { H a = make (); H b = a; other (); return use (a); }
// What a call got escapes.
int k8 () { H a = make (); by_val (a); contract_assert<never_proven_symbolic_v>(warm (a)); return use (a); }
int k9 () { H a = make (); by_val (a); contract_assert<never_proven_symbolic_v>(warm (a)); other (); return use (a); } // { dg-warning "cannot verify" }
int k10 () { H a = make (); g = a; return use (a); }
int k11 () { H a = make (); g = a; other (); return use (a); } // { dg-warning "cannot verify" }
// Written through its pointer here.
int k12 () { H a = make (); a.p->x = 1; return use (a); } // { dg-warning "cannot verify" }
// No pointers; a conveyor callee; a value fact.
int k13 () { N n = make_n (); by_val_n (n); return use_n (n); }
int k14 () { H a = make (); conv_by_val (a); return use (a); }
int k15 () { H a = make_hot (); by_val (a); return use_hot (a); }
