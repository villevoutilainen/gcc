// D4324: a reference bound to a temporary ('const V &r = make ();')
// refers to an object, the temporary whose lifetime is extended to the
// reference's.  It is bound through '(D = make (), &D)', which the AST
// engine didn't see through, so r's conveyor member couldn't be called.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct V { int c; int cat () const conveyor { return c; } };
V make ();

int bound () { const V &r = make (); return r.cat (); }
int moved () { V &&r = make (); return r.cat (); }
