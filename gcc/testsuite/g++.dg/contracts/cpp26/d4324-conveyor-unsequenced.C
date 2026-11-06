// D4324: in a conveyor function or predicate, an object mustn't be
// modified unsequenced with another access to it ([intro.execution]),
// nor with an access to what may be the same object: two references a
// caller may bind to one object.  Such code is written as separate
// statements instead.  A call isn't checked: what the callee does is
// checked in its own body (P4445 BUG-12).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
using namespace std::contracts;

struct P { unsigned x; unsigned y; };
void g (unsigned, unsigned) conveyor;
unsigned f (unsigned) conveyor;

unsigned bad1 (unsigned i) conveyor { return i++ + i++; } // { dg-error "'i' is modified unsequenced with another access to it" }
unsigned bad2 (unsigned i) conveyor { return f (i++) + i; } // { dg-error "'i' is modified unsequenced" }
unsigned inc2 (unsigned& a, unsigned& b) conveyor { return a++ + b++; } // { dg-error "'a' is modified unsequenced with an access to 'b', which may be the same object" }
unsigned bad3 (unsigned& a, unsigned& b) conveyor { return (a = 1) + b; } // { dg-error "may be the same object" }
unsigned bad4 (unsigned& r) conveyor { unsigned l = 0; unsigned& m = l; return r++ + m++; } // { dg-error "may be the same object" }
unsigned bad5 (P& p, unsigned& q) conveyor { return p.x++ + q; } // { dg-error "may be the same object" }
unsigned bad6 (unsigned x) conveyor { return [] (unsigned a) { return a++ + a; } (x); } // { dg-error "'a' is modified unsequenced" }
bool bad7 (unsigned x) pre<conveyor_assert_v> ([] (unsigned a) { return a++ + a > 0; } (x)); // { dg-error "'a' is modified unsequenced" }

// The same object passed twice: what inc2 does with it is checked in
// inc2.
unsigned w (unsigned x) conveyor { return inc2 (x, x); }

// Sequenced (C++17), or certainly different objects.
unsigned ok1 (unsigned i) conveyor { i = i + 1; i = i++; return i; }
void ok2 (unsigned i) conveyor { g (i++, i++); }
unsigned ok3 (P& p) conveyor { return p.x++ + p.y++; }
unsigned ok4 (unsigned& r) conveyor { unsigned l = 0; return r++ + l++; }
unsigned ok5 (unsigned i) conveyor { return (i++, i) && i; }
unsigned ok6 (unsigned i) conveyor { return i ? i++ : i--; }
unsigned ok7 (unsigned& a, unsigned& b) conveyor { a++; b++; return a + b; }
