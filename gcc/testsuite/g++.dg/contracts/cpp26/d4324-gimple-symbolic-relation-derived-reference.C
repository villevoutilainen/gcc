// D4324: a variable holding a class value obtained from an object (the
// derived-value rule, for symbolic relations) is an object of its own: a
// call that may end that object ends the value's relations, not the
// variable's own object facts, nor those of a reference bound to it.
// (BDE bdlt_calendar: 'It m = c.begin (); const It &a = m; touch (c);'
// made 'a' unprovable.)  The gimple engine.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs -fcontract-analysis-engine=gimple" }

#include <contracts>
using namespace std::contracts;

struct It { int *p; };
struct Cal { int d[4]; It begin () const; };
bool reachable (It, It) symbolic;		// the translation unit declares one
bool same (const It &a, const It &b) conveyor { return a.p == b.p; }
void touch (const Cal &);

bool f (const Cal &c)
{
  It m = c.begin ();
  const It &a = m;
  touch (c);
  return same (a, a);
}
