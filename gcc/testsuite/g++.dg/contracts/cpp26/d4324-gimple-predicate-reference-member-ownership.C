// D4324: d4324-predicate-reference-member-ownership.C under the GIMPLE engine.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>

namespace sc = std::contracts;

struct R { int &r; };
struct C { const int &r; };
struct P { int *p; };

bool setr (R s) conveyor { s.r = 7; return true; }
bool getc (C s) conveyor { return s.r > 0; }
bool getp (P s) conveyor { return s.p != nullptr; }

void f (int &x)
  pre<sc::conveyor_assert_v>(setr (R{x}));	// { dg-error "a temporary of type 'R' holds a reference to an object not owned" }

void g (int &x)
{
  contract_assert<sc::conveyor_assert_v>(
    [&] () conveyor { x = 5; return true; } ());	// { dg-error "holds a reference to an object not owned" }
}

// Still fine: a reference to const, a pointer member, a capture by value,
// and a conveyor function's own body.

void h (int &x)
  pre<sc::conveyor_assert_v>(getc (C{x}));

void k (int *q)
  pre<sc::conveyor_assert_v>(getp (P{q}));

void m (int x)
{
  contract_assert<sc::conveyor_assert_v>([x] () conveyor { return x > 0; } ());
}

bool body (int &x) conveyor { return setr (R{x}); }
