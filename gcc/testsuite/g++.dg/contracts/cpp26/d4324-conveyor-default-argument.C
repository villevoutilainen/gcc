// D4324: a default argument is evaluated by the caller, so the conveyor
// rules it is checked against are the caller's, never the callee's: in a
// conveyor caller a default argument violating them is an error, whether
// the callee is conveyor or not; in any other caller there is nothing to
// check, except that its value meets the callee's preconditions, as an
// argument written in the call would.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

int nc ();
int cv () conveyor;

int g (int x = nc ()) conveyor;
int ok (int x = cv ()) conveyor;
struct S { int m (int x = nc ()) conveyor; };
template<class T> int h (T x = T (nc ())) conveyor { return 0; }
template<class T> auto ca (T) conveyor(auto) { return g (); }
int pr (const int &r = *(const int *) nullptr) conveyor { return 0; }

int plain () { return g () + S ().m () + h<int> () + ok () + ca (1); }
int plain2 () { return pr (); } // { dg-error "cannot prove 'is_object_address'" }

int f1 () conveyor { return g (); }	// { dg-error "default argument for 'x' of 'int g\\(int\\)'" }
int f2 () conveyor { return S{}.m (); }	// { dg-error "default argument for 'x' of 'int S::m\\(int\\)'" }
// The instantiated default argument was cached by plain ().
int f3 () conveyor { return h<int> (); }	// { dg-error "default argument for 'x' of 'int h\\(T\\) \\\[with T = int\\\]'" }
int f4 () conveyor { return ok (); }
int f5 () conveyor { return ca (1); }	// { dg-error "call to 'auto ca\\(T\\) \\\[with T = int\\\]', which is not declared 'conveyor'" }
int f6 () conveyor { return pr (); }	// { dg-error "cannot prove 'is_object_address'" }

// { dg-prune-output "declared here" }
// { dg-prune-output "used by" }
