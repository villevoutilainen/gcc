// D4324: a fact relating a value to an accessor's result on an object
// (a precondition's 'x < v.size ()') ends when the object may have
// changed: a write to one of its fields, or a call that is handed it by
// non-const reference or pointer, or a non-const member function call.
// A const reference or a const member function keeps it.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct S {
  int n;
  int size () const conveyor { return n; }
  void shrink () conveyor { n = 0; }
};
void reset (S& v) conveyor { v.n = 0; }
void look (const S& v) conveyor { }
int consume (int x, const S& v) conveyor pre<sc::conveyor_assert_v> (x < v.size ()) { return x; }

// The precondition's fact holds until V changes.
int kept (int x, S& v) conveyor pre<sc::conveyor_assert_v> (x < v.size ())
{
  contract_assert<sc::conveyor_assert_v> (x < v.size ());
  look (v);
  (void) v.size ();
  consume (x, v);
  return consume (x, v);
}

// A field write, a call with V by non-const reference, and a non-const
// member function call each end it.
int field_write (int x, S& v) conveyor pre<sc::conveyor_assert_v> (x < v.size ())
{
  v.n = 0;
  contract_assert<sc::conveyor_assert_v> (x < v.size ()); // { dg-warning "cannot verify .contract_assert. condition" }
  return 0;
}
int by_reference (int x, S& v) conveyor pre<sc::conveyor_assert_v> (x < v.size ())
{
  reset (v);
  return consume (x, v); // { dg-warning "cannot verify that .x. satisfies the precondition" }
}
int member_call (int x, S& v) conveyor pre<sc::conveyor_assert_v> (x < v.size ())
{
  v.shrink ();
  contract_assert<sc::conveyor_assert_v> (x < v.size ()); // { dg-warning "cannot verify .contract_assert. condition" }
  // Checked, so it holds again.
  return consume (x, v);
}

