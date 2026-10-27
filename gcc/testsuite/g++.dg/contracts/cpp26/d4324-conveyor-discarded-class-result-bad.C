// D4324: a call whose result is a class returned by invisible reference
// (not trivially copyable) and discarded is an AGGR_INIT_EXPR in a
// TARGET_EXPR: the AST engine didn't scan it, so its obligations -- a
// member function's 'this' among them -- went unchecked.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>

struct D { int *p; ~D () {} };
struct C { int *p; C () {} C (const C &o) : p (o.p) {} };
struct T { T (int &r) pre<std::contracts::conveyor_assert_v> (std::is_object_address (&r)); ~T () {} };

struct L
{
  D getd () pre<std::contracts::conveyor_assert_v> (std::is_object_address (this));
  C getc () pre<std::contracts::conveyor_assert_v> (std::is_object_address (this));
  void f1 () { getd (); }	// { dg-error "cannot prove .is_object_address." }
  void f2 () { getc (); }	// { dg-error "cannot prove .is_object_address." }
  void f3 () { (void) getd (); } // { dg-error "cannot prove .is_object_address." }
};

void g (int *p)
{
  T {*p};	// { dg-error "cannot prove .is_object_address." }
}
