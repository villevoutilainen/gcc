// D4324 (GIMPLE engine): 'if (m.toggle ())' makes m.toggle () a fact about m
// only if calling it again gives the same result.  Not so for a member of a
// class with a mutable member, which even a const member function may write
// (P4445 BUG-21).  A conveyor function now writes one through const access
// only to an object that began its lifetime in it, so toggle () is itself
// ill-formed; the call still isn't taken as a fact.  A const accessor of a class without one, or a function
// taking a const reference, is fine.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>

namespace sc = std::contracts;

struct M
{
  mutable bool flip = false;
  bool toggle () const conveyor { flip = !flip; return flip; } // { dg-error "write to mutable member" }
};
void need (const M &m) pre<sc::proven_conveyor_v>(m.toggle ()) {}
int a () { M m; if (m.toggle ()) need (m); return 0; } // { dg-error "cannot prove the precondition" }

struct P
{
  bool on = false;
  bool get () const conveyor { return on; }
};
void needp (const P &p) pre<sc::proven_conveyor_v>(p.get ()) {}
int b () { P p; if (p.get ()) needp (p); return 0; }

bool ok (const P &p) conveyor { return p.on; }
void needf (const P &p) pre<sc::proven_conveyor_v>(ok (p)) {}
int c () { P p; if (ok (p)) needf (p); return 0; }
