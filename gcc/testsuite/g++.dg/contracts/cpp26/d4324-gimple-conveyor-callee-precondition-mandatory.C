// D4324: d4324-conveyor-callee-precondition-mandatory.C under the GIMPLE
// engine.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>
using std::contracts::conveyor_assert_v;
using std::contracts::never_proven_conveyor_v;

int dv (int n) conveyor pre<conveyor_assert_v> (n != 0);
int pos (int x) conveyor pre<conveyor_assert_v> (x > 0);
int lt (int x, int n) conveyor pre<conveyor_assert_v> (x < n);
void byref (int& x) conveyor pre<conveyor_assert_v> (x < 100);

// Conveyor function bodies.
int b1 (int b) conveyor { return dv (b); }			// { dg-error "cannot prove that .b. is nonzero" }
int b1ok (int b) conveyor { return b != 0 ? dv (b) : 0; }
int b2 (int x) conveyor { return pos (x); }		// { dg-error "cannot prove that .x. satisfies the precondition" }
int b2ok (int x) conveyor { return x > 0 ? pos (x) : 0; }
int b3 (int x, int n) conveyor { return lt (x, n); }	// { dg-error "cannot prove that .x. satisfies the precondition" }
int b3ok (int x, int n) conveyor { if (x < n) return lt (x, n); return 0; }

// Strict predicates, never-proven ones included.
void p1 (int b) pre<conveyor_assert_v> (dv (b) > 0) {}		// { dg-error "cannot prove that .b. is nonzero" }
void p1ok (int b) pre<conveyor_assert_v> (b != 0 && dv (b) > 0) {}
void p2 (int x) pre<conveyor_assert_v> (pos (x) > 0) {}		// { dg-error "cannot prove that .x. satisfies the precondition" }
int  p3 (int a) post<conveyor_assert_v> (r: pos (r) > 0) { return a; }	// { dg-error "cannot prove that \[^\n\]* satisfies the precondition" }
void p4 (int b) { contract_assert<conveyor_assert_v> (dv (b) > 0); }	// { dg-error "cannot prove that .b. is nonzero" }
void np (int b) pre<never_proven_conveyor_v> (dv (b) > 0) {}	// { dg-error "cannot prove that .b. is nonzero" }
void npok (int b) pre<never_proven_conveyor_v> (b != 0 && dv (b) > 0) {}

// A reference parameter: the value of the object it is bound to.
void r1 () conveyor { int local = 0; byref (local); }
void r2 (int y) conveyor { if (y < 50) byref (y); }
void r3 () conveyor { int local = 200; byref (local); }		// { dg-error "provably violates the precondition" }
void r4 (int y) conveyor { byref (y); }				// { dg-error "cannot prove that .y. satisfies the precondition" }

// An address-taken local, passed by value.
void byval (int x) conveyor pre<conveyor_assert_v> (x < 100);
void a1 () conveyor { int local = 0; const int* p = &local; byval (local); (void) p; }

// this, and the address of an object, are nonzero.
struct S
{
  int use (S* p) conveyor pre<conveyor_assert_v> (p != nullptr) { return 1; }
  int self () conveyor { return use (this); }
};
int use_ptr (const int* p) conveyor pre<conveyor_assert_v> (p != nullptr);
int addr () conveyor { int v = 1; return use_ptr (&v); }

// Ordinary code: a proof warning only with -fcontract-conveyor-proofs.
int ord (int b) { return dv (b); }

// conveyor(auto): breaking the rule makes the specialization not
// conveyor, rather than being an error.
template <class T> T dd (T b) conveyor(auto) { return dv (b); }
int use_dd () { return dd (2); }
