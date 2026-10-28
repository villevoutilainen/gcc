// D4324: a call in an ordinary (not conveyor) contract_assert's condition
// owes its callee's preconditions, checked against the facts where the
// assertion is, as the same call in the function's body would be.  With
// control objects the condition is outlined into its own function; the
// GIMPLE engine used to analyze that only for a conveyor or never_proven
// contract, so these calls went unchecked there.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>

struct D
{
  int n = 0;
  bool empty () const
    pre<std::contracts::conveyor_assert_v> (std::is_object_address (this));
  void unproven ();
  void proven ();
  void inpre () pre (!this->empty ());	// The callers' obligation.
};

bool D::empty () const { return n == 0; }

void D::unproven ()
{
  contract_assert (!this->empty ());	// { dg-error "cannot prove 'is_object_address'" }
}

void D::proven ()
{
  D d;
  contract_assert (d.empty ());
}

void D::inpre () { }

void f1 (D *p)
{
  contract_assert (!p->empty ());	// { dg-error "cannot prove 'is_object_address'" }
}

void f2 ()
{
  D d;
  D *p = &d;
  contract_assert (p->empty () || true);
}
