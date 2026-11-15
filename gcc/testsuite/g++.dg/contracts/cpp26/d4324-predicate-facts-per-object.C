// D4324: several predicate facts about one object: 'ok (p)' and 'p.get ()'
// both hold after 'if (ok (p) && p.get ())', the second no longer replacing
// the first.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=ast" }

#include <contracts>
namespace sc = std::contracts;

struct P { bool on = false; bool get () const conveyor { return on; } };
bool ok (const P &p) conveyor { return p.on; }
void needboth (const P &p)
  pre<sc::proven_conveyor_v> (ok (p)) pre<sc::proven_conveyor_v> (p.get ());
void needn (const P &p) pre<sc::proven_conveyor_v> (!ok (p));

// Two facts about one object, from one condition or two.
int both_and () { P p; if (ok (p) && p.get ()) needboth (p); return 0; }
int both_nested () { P p; if (ok (p)) if (p.get ()) needboth (p); return 0; }
int both_reversed () { P p; if (p.get ()) if (ok (p)) needboth (p); return 0; }
int both_early () { P p; if (!ok (p) || !p.get ()) return 0; needboth (p); return 1; }

// One of them only, the wrong polarity, another object: not proven.
int one_only () { P p; if (ok (p)) needboth (p); return 0; } // { dg-error "cannot prove that .bool P::get\\(\\) const." }
int polarity () { P p; if (ok (p) && p.get ()) needn (p); return 0; } // { dg-error "cannot prove|provably" }
int other () { P p, q; if (ok (q) && q.get ()) needboth (p); return 0; } // { dg-error "cannot prove" }
