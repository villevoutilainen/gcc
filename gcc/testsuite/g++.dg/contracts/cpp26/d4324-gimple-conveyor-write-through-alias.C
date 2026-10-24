// D4324: the GIMPLE engine's twin of d4324-conveyor-write-through-alias.C.
// A store through a pointer or reference to an object, or a call
// that may write through one, ends what was known about the object's
// value: what the pointer is known to point to ('p = &k'), or, when that
// isn't known, any of the function's objects whose address was taken.
// Writing 'k' through 'p' or 'r' used to leave k's range, nonzero and
// field facts in place.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs -fcontract-analysis-engine=gimple" }

#include <contracts>
using std::contracts::conveyor_assert_v;

void big (int x) conveyor pre<conveyor_assert_v> (x > 5);
void nzv (int x) conveyor pre<conveyor_assert_v> (x != 0);
void ne1 (int x) conveyor pre<conveyor_assert_v> (x != 1);
int *pick (int *a, int *b) conveyor;
struct S { int f; };
void bigf (S &s) conveyor pre<conveyor_assert_v> (s.f > 5);

// Written through an alias: no longer known.
void a1 (int k) conveyor { if (k > 5) { int *p = &k; *p = 1; big (k); } }	// { dg-error "cannot prove that .k. satisfies" }
void a2 (int k) conveyor { if (k != 0) { int *p = &k; *p = 0; nzv (k); } }	// { dg-error "cannot prove that .k. is nonzero" }
void a3 (int k) conveyor { if (k > 5) { int &r = k; r = 1; big (k); } }	// { dg-error "cannot prove that .k. satisfies" }
void a4 (int k) conveyor { if (k != 1) { int *p = &k; *p = 1; ne1 (k); } }	// { dg-error "cannot prove that .k. satisfies" }
void a5 (int k) { if (k > 5 && k < 50) { int *p = &k; ++*p; *p -= 40; big (k); } } // { dg-warning "cannot verify that .k. satisfies" }
void a6 (int k, int j) { if (k > 5) { int *p = pick (&k, &j); *p = 1; big (k); } } // { dg-warning "cannot verify that .k. satisfies" }
void a7 (int k) conveyor { if (k > 5) { int *p = &k; int *q = p; *q = 1; big (k); } } // { dg-error "cannot prove that .k. satisfies" }
void a8 (int k) conveyor { if (k > 5) { int *p = &k; p[0] = 1; big (k); } }	// { dg-error "cannot prove that .k. satisfies" }
void a9 () conveyor { S s{9}; S *p = &s; p->f = 1; bigf (s); }		// { dg-error "provably violates" }

// The store doesn't reach K, or its field is rewritten provably.
void o1 (int k, int j) conveyor { if (k > 5) { int *p = &j; *p = 1; big (k); } }
void o2 () conveyor { S s{1}; S *p = &s; p->f = 9; bigf (s); }
void o3 (int k) conveyor { if (k > 5) { int m = 0; int *p = &m; *p = 1; big (k); } }

// A call that may write through a pointer or reference to K.
void touch (int &);
void touchp (int *);
void ctouch (const int &);
void c1 (int k) { if (k > 5) { int *p = &k; touch (*p); big (k); } }	// { dg-warning "cannot verify that .k. satisfies" }
void c2 (int k) { if (k > 5) { int *p = &k; touchp (p); big (k); } }	// { dg-warning "cannot verify that .k. satisfies" }
void c3 (int k) { if (k > 5) { int &r = k; touch (r); big (k); } }	// { dg-warning "cannot verify that .k. satisfies" }
void c4 (int k, int j) { if (k > 5) { int *p = &j; touchp (p); big (k); } }

// { dg-prune-output "no fact relating this value" }
// { dg-prune-output "a numeric range is known" }
