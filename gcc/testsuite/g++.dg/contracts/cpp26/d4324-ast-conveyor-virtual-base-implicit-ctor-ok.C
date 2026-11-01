// D4324: the implicitly-defined default constructor of a class with a
// virtual base is conveyor when every base's and member's is (P2680
// 9.2.3), and its body reaches the virtual base through the C++ ABI's
// own pointers.  Before cloning, a constructor that may run as either the
// complete- or the base-object variant selects the virtual base's offset
// on its in-charge parameter, 'this + (__in_chrg == 0 ? *(vptr - 24) :
// 8)' (AST engine); the complete-object clone uses the fixed offset,
// 'this + 8' (GIMPLE engine).  Both were rejected as unprovable
// dereferences.  An abstract class's constructor ignores its virtual
// bases for construction (DR 1658) but still sets their vtable pointers.
// A base of a virtual base is reached at the virtual base's offset plus
// a constant, '*(vptr - 24) + 16', which also isn't the program's own
// arithmetic to prove free of overflow.  (Qt's QTlsPrivate::
// DtlsCookieVerifier, 'virtual public DtlsBase'.)
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=ast" }

#include <contracts>

// An abstract class with a virtual base, and its concrete derived class;
// only D's constructor is conveyor here (E's needs the virtual base's
// destructor, which isn't).
struct B0 { virtual ~B0 () = default; int x = 0; };
struct D0 : virtual B0 { virtual void f () = 0; };
struct E0 : D0 { void f () override {} };

int g () { E0 e; return e.x; }

// The same, and more, constructed and destroyed in a conveyor function.
struct B { int x = 0; ~B () conveyor {} };
struct D : virtual B { virtual void f () = 0; };
struct E : D { void f () override {} };

// A non-abstract class with a virtual base.
struct N : virtual B { int y = 1; };

// A diamond.
struct L : virtual B { int l = 2; };
struct R : virtual B { int r = 3; };
struct J : L, R { int j = 4; };

// A virtual base with a polymorphic non-primary base of its own.
struct P { virtual void p () {} int pv = 5; };
struct Q { virtual void q () {} int qv = 6; };
struct V : P, Q { int vv = 7; };
struct W : virtual V { virtual void w () = 0; };
struct X : W { void w () override {} };
struct Y : virtual V, virtual B { int yy = 8; };

// A virtual base with a member whose destructor is conveyor.
struct C { B b; ~C () conveyor {} };
struct Z : virtual C { };

int use_e () conveyor { E e {}; return e.x; }
int use_n () conveyor { N n {}; return n.x; }
int use_j () conveyor { J j {}; return j.x; }
int use_x () conveyor { X x {}; return x.qv; }
int use_y () conveyor { Y y {}; return y.qv; }
int use_z () conveyor { Z z {}; return z.b.x; }
int use_copy () conveyor { N n {}; N m (n); m = n; return m.y; }
