// D4324: a symbolic function's by-value parameter is a value, its facts
// keyed by the value's bytes; of a class type, that type must have unique
// object representations.  Any other class is taken by reference.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct P { char c; int i; };			// padding
struct N { int v; N (const N &o) : v (o.v) {} };	// not trivially copyable
struct U { int v; };
struct I;

bool a (P) symbolic;		// { dg-error "unique object representations, which .P." }
// { dg-message "take it by reference" "" { target *-*-* } .-1 }
bool b (N) symbolic;		// { dg-error "unique object representations, which .N." }
// { dg-message "take it by reference" "" { target *-*-* } .-1 }
bool c (U, int) symbolic;
bool d (const N &, const P &) symbolic;
bool e (I) symbolic;		// incomplete: reported where a contract uses it
bool f (double, float) symbolic;
bool g (const P *) symbolic;
struct M { bool m (P) const symbolic; };	// { dg-error "unique object representations" }
// { dg-message "take it by reference" "" { target *-*-* } .-1 }
template <class T> bool t (T) symbolic;
