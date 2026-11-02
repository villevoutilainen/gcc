// D4324: a call in an if condition that isn't conveyor ends the objects
// it is given a route to on both paths out of it, the false one too
// (short-circuited: only the conjuncts evaluated); only the arm a
// constant condition selects is evaluated; a structured binding
// declaration's hidden variable is trusted at each binding; a named
// return value's destructor runs only on an exception path.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>
#include <tuple>
#define ALIVE(x) contract_assert<std::contracts::never_proven_conveyor_v> (std::is_object_address (&x))

struct H { int v; void cm () const conveyor {} bool ok () const conveyor { return true; } int nm () const; };
bool touch (H&);
bool p ();
H g;

void false_path () { H l; ALIVE (l); if (touch (l)) return; l.cm (); } // { dg-error "cannot prove .is_object_address." }
void true_path () { H l; ALIVE (l); if (touch (l)) l.cm (); } // { dg-error "cannot prove .is_object_address." }
void second_conjunct () { H l; ALIVE (l); if (l.ok () && touch (l)) return; l.cm (); } // { dg-error "cannot prove .is_object_address." }
void unrelated_conjunct () { H l; ALIVE (l); if (p () && l.ok ()) return; l.cm (); }
void member_call () { H l; ALIVE (l); if (l.nm () == 1) return; l.cm (); } // { dg-error "cannot prove .is_object_address." }
int ternary () { H l; ALIVE (l); return touch (l) ? 1 : (l.cm (), 2); } // { dg-error "cannot prove .is_object_address." }
void dead_arm () { (void) (p () ? (void) (true ? true : (g.cm (), true)) : (void) 0); }

struct P
{
  H m;
  int ty () const;
  void f () const { if (ty () == 1) return; m.cm (); } // { dg-error "cannot prove .is_object_address." }
};
void use (P& x) { ALIVE (x); x.f (); }

int bindings (std::tuple<int, int>& t) { auto [a, b] = t; return a + b; }

struct S { S (); S (S&&); S (const S&); ~S () conveyor {} void nm (); };
S nrv () { S s; s.nm (); ALIVE (s); return s; }
