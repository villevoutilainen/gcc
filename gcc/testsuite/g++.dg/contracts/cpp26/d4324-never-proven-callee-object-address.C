// D4324: a never_proven conveyor assertion trusts its own claim, not what
// the calls it makes require: a conveyor callee's is_object_address
// preconditions -- implicit (reference parameters, 'this') and explicit --
// must still be established inside it, unless never_proven themselves.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=ast" }

#include <contracts>
using std::contracts::never_proven_conveyor_v;

struct S
{
  int n;
  friend constexpr bool eq (const S& x, int v) conveyor { return x.n == v; }
  constexpr bool mem (int v) const conveyor { return n == v; }

  int a () const
  { contract_assert<never_proven_conveyor_v> (!eq (*this, 0)); return n; }	// { dg-error "cannot prove .is_object_address." }
  int b () const
  { contract_assert<never_proven_conveyor_v> (!mem (0)); return n; }	// { dg-error "cannot prove .is_object_address." }
  int c (const S& o) const
  { contract_assert<never_proven_conveyor_v> (!eq (o, 0)); return n; }	// { dg-error "cannot prove .is_object_address." }

  // A never_proven assertion of the receiver's or parameter's validity
  // establishes it for the ones after it.
  int ok1 () const
  {
    contract_assert<never_proven_conveyor_v> (std::is_object_address (this));
    contract_assert<never_proven_conveyor_v> (!eq (*this, 0));
    contract_assert<never_proven_conveyor_v> (!mem (0));
    return n;
  }
  int ok2 (const S& o) const
  {
    contract_assert<never_proven_conveyor_v> (std::is_object_address (&o));
    contract_assert<never_proven_conveyor_v> (!eq (o, 0));
    return n;
  }
};

constexpr bool deref (const int* p) conveyor
  pre<never_proven_conveyor_v> (std::is_object_address (p))
{ return *p == 0; }

void d (const int* p)
{ contract_assert<never_proven_conveyor_v> (!deref (p)); }	// deref's precondition is never_proven itself: nothing to prove

void ok3 (const int* p)
{
  contract_assert<never_proven_conveyor_v> (std::is_object_address (p));
  contract_assert<never_proven_conveyor_v> (!deref (p));
}

int f (S& s, const int* p)
{
  d (p);
  ok3 (p);
  return s.a () + s.b () + s.c (s) + s.ok1 () + s.ok2 (s);
}
