// D4324: the counterpart of d4324-conveyor-discarded-class-result-bad.C --
// the same discarded calls, with what they require proven.
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
  void f1 () pre<std::contracts::conveyor_assert_v> (std::is_object_address (this))
  { getd (); }
  void f2 () pre<std::contracts::conveyor_assert_v> (std::is_object_address (this))
  { getc (); }
  void f3 () pre<std::contracts::conveyor_assert_v> (std::is_object_address (this))
  { (void) getd (); }
};

void g ()
{
  L l;
  l.getd ();
  l.getc ();
  int i = 0;
  T {i};
}
