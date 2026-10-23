// D4324: a constructor's compiler-generated destruction of its own
// members, on the exception path from a call that may have ended 'this'
// (a non-conveyor call handed a parameter that may alias it), trusts
// 'this': the shape of basic_string's copy constructor.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct M { int v = 0; ~M () conveyor {} };

struct S
{
  M m;
  int get () const;
  void use () conveyor {}
  S (const S& o) pre<sc::never_proven_conveyor_v> (std::is_object_address (&o))
  {
    int n = o.get ();
    contract_assert<sc::never_proven_conveyor_v> (std::is_object_address (this));
    use ();
    (void) n;
  }
};

void copy (const S& s) pre<sc::never_proven_conveyor_v> (std::is_object_address (&s))
{
  S t (s);
}
