// D4324: an assertion's condition, outlined into its own function under
// control objects, knows what its check site knew about the fields of
// the objects it captures -- 'this->c' trusted earlier -- as the same
// expression in the function's body would.  Not what a call or store in
// between could have changed, nor what holds on one branch only.
// A conveyor callee can't end its arguments, but it can store in them.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>
using namespace std::contracts;
struct V {
  int s = 0;
  int size () const conveyor;
};
int V::size () const conveyor { return s; }
struct C { V keys; };
C *unknown ();
bool cond ();
struct It {
  C *c; int i;
  int trusted () const;
  int untrusted () const;
  int reassigned ();
  int one_branch () const;
  int conv () const pre<conveyor_assert_v> (std::is_object_address (this));
  int after_call ();
  int conveyor_call ()
    pre<conveyor_assert_v> (std::is_object_address (this));
  void mutate ();
  void reset (C *p) conveyor;
};
int It::trusted () const
{
  contract_assert<never_proven_conveyor_v> (std::is_object_address (c));
  contract_assert (i < c->keys.size ());
  return i;
}
int It::untrusted () const
{
  contract_assert (i < c->keys.size ());	// { dg-error "cannot prove" }
  return i;
}
int It::reassigned ()
{
  contract_assert<never_proven_conveyor_v> (std::is_object_address (c));
  c = unknown ();
  contract_assert (i < c->keys.size ());	// { dg-error "cannot prove" }
  return i;
}
int It::one_branch () const
{
  if (cond ())
    contract_assert<never_proven_conveyor_v> (std::is_object_address (c));
  contract_assert (i < c->keys.size ());	// { dg-error "cannot prove" }
  return i;
}
int It::conv () const
{
  contract_assert<never_proven_conveyor_v> (std::is_object_address (c));
  contract_assert<conveyor_assert_v> (i < c->keys.size ());
  return i;
}
int It::after_call ()
{
  contract_assert<never_proven_conveyor_v> (std::is_object_address (c));
  mutate ();
  contract_assert (i < c->keys.size ());	// { dg-error "cannot prove" }
  return i;
}
void It::reset (C *p) conveyor { c = p; }
int It::conveyor_call ()
{
  contract_assert<never_proven_conveyor_v> (std::is_object_address (c));
  reset (nullptr);
  contract_assert (i < c->keys.size ());	// { dg-error "cannot prove" }
  return i;
}
int local (int i)
{
  It it { unknown (), i };
  contract_assert<never_proven_conveyor_v> (std::is_object_address (it.c));
  contract_assert (i < it.c->keys.size ());
  return i;
}
int local_untrusted (int i)
{
  It it { unknown (), i };
  contract_assert (i < it.c->keys.size ());	// { dg-error "cannot prove" }
  return i;
}
