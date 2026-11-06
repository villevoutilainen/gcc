// D4324: an object whose address was obtained from another object -- a
// reference or pointer a call given a route to it returned, or a pointer
// loaded from its memory -- may be one that object owns ('int &r = v[0]'
// into a vector's buffer).  A call that may end the object it was obtained
// from may end it too, so its is_object_address fact ends with it (P4445
// caller9: an element reference kept across a reallocating push_back was
// accepted as a proven object address).  A call given no route to that
// object keeps it.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

namespace sc = std::contracts;

struct V {
  int *data;
  int &at (int)
    post<sc::never_proven_conveyor_v>(r: std::is_object_address (&r));
  const int &cat (int) const
    post<sc::never_proven_conveyor_v>(r: std::is_object_address (&r));
  int *ptr (int)
    post<sc::never_proven_conveyor_v>(r: std::is_object_address (r));
  void grow ();				// may reallocate DATA
};
struct W { V v; };
void g (V &);
void other ();

int use (const int &r) conveyor { return r; }

int ref_then_member_call ()
{
  V v{};
  int &r = v.at (0);
  v.grow ();
  return use (r);	// { dg-error "cannot prove 'is_object_address'" }
}

int pointer_then_member_call ()
{
  V v{};
  int *p = v.ptr (0);
  v.grow ();
  return use (*p);	// { dg-error "cannot prove 'is_object_address'" }
}

int ref_then_call_given_object ()
{
  V v{};
  int &r = v.at (0);
  g (v);
  return use (r);	// { dg-error "cannot prove 'is_object_address'" }
}

int through_member_subobject ()
{
  W w{};
  int &r = w.v.at (0);
  w.v.grow ();
  return use (r);	// { dg-error "cannot prove 'is_object_address'" }
}

int const_accessor ()
{
  V v{};
  const int &r = v.cat (0);
  v.grow ();
  return use (r);	// { dg-error "cannot prove 'is_object_address'" }
}

int loaded_pointer ()
{
  V v{};
  int *p = v.data;
  contract_assert<sc::never_proven_conveyor_v>(std::is_object_address (p));
  v.grow ();
  return use (*p);	// { dg-error "cannot prove 'is_object_address'" }
}

// Still proven: no call in between, a call given no route to V, and a
// call that may end another object.

int no_call ()
{
  V v{};
  int &r = v.at (0);
  return use (r);
}

int unrelated_call ()
{
  V v{};
  int &r = v.at (0);
  other ();
  return use (r);
}

int other_object ()
{
  V a{}, b{};
  int &r = a.at (0);
  b.grow ();
  return use (r);
}

int loaded_pointer_no_call ()
{
  V v{};
  int *p = v.data;
  contract_assert<sc::never_proven_conveyor_v>(std::is_object_address (p));
  return use (*p);
}
