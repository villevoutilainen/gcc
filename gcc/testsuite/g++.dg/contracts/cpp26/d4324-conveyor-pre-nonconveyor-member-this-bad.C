// D4324/P2680: the member-function/'this' sibling of d4324-conveyor-
// pre-nonconveyor-function-bad.C -- a conveyor-flavored pre<> on a
// member function that is NOT itself declared 'conveyor' must still
// synthesize the implicit is_object_address(this) obligation a real
// 'conveyor' member function already gets. this's own synthesis is
// already looped over the same way an ordinary reference parameter is
// (oa_synthesize_implicit_reference_safety_preconditions), so fixing
// the gate for reference parameters covers 'this' automatically.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>

struct S {
  int m_value;
  int f () pre<std::contracts::conveyor_assert_v> (this->m_value < 2048)
  { return m_value; }
};

void h (S* p)
{
  p->f (); // { dg-error "cannot prove .is_object_address. for .p." }
	   // { dg-message "no fact relating this value" "unprovable reason" { target *-*-* } .-1 }
	   // { dg-warning "cannot verify that field .S::m_value." "" { target *-*-* } .-2 }
}
