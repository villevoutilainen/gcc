// D4324/P2680 9.2.3: a default member initializer is part of the
// constructors that use it.  One that violates the conveyor rules makes a
// defaulted constructor non-conveyor, and is an error in a conveyor
// constructor or aggregate initialization; and a defaulted constructor
// is conveyor only if the destructors of the members it constructs are,
// for when a later member's initialization throws.  Without any conveyor
// function in sight, none of this is diagnosed.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct P { P (int *) { } };			// not conveyor
struct D { D () conveyor = default; ~D () { } };	// destructor isn't
int *mk ();

struct C { P p = nullptr; C () = default; };
struct I : C { using C::C; };
struct A { P p = nullptr; };			// aggregate
struct M { D d; int i = 0; M () = default; };
struct Ok { int i = 1; Ok () = default; };
struct X { P p = nullptr; X () conveyor = default; }; // { dg-error "default member initializer for 'X::p'" }
struct E { P p = nullptr; E () conveyor { } };	// { dg-error "default member initializer for 'E::p'" }
struct F { int i = *mk (); F () conveyor { } };	// { dg-error "default member initializer for 'F::i'" }
template<class T> struct G { P p = nullptr; G () conveyor { } }; // { dg-error "default member initializer for 'G<int>::p'" }

void plain () { C c; I i; A a; M m; Ok o; X x; G<int> g; }

void f1 () conveyor { C c{}; }	// { dg-error "call to 'constexpr C::C\\(\\)', which is not declared 'conveyor'" }
void f2 () conveyor { I i{}; }	// { dg-error "call to 'constexpr I::I\\(\\)', which is not declared 'conveyor'" }
void f3 () conveyor { A a{}; }	// { dg-error "default member initializer for 'A::p'" }
void f4 () conveyor { M m{}; }	// { dg-error "call to 'constexpr M::M\\(\\)', which is not declared 'conveyor'" }
void f5 () conveyor { Ok o{}; }
void f6 () conveyor { X x{}; }

// { dg-prune-output "declared here" }
// { dg-prune-output "used by" }
