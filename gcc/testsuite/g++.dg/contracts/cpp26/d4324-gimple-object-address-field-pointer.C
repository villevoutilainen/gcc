// D4324: in the GIMPLE engine, 'is_object_address (this->ptr)' in a
// precondition or contract_assert says the pointer field holds an
// object's address, so a call through the loaded pointer is provable,
// as in the AST engine -- e.g. a guard's destructor
// 'pre (is_object_address (_M_this)) { _M_this->f (); }'.  A store to
// the field replaces the fact.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>
using namespace std::contracts;

struct T { void f () conveyor; };

struct guard
{
  T *t;
  ~guard ()
    pre<never_proven_conveyor_v> (std::is_object_address (this))
    pre<never_proven_conveyor_v> (std::is_object_address (t))
  { t->f (); }

  void asserted ()
    pre<never_proven_conveyor_v> (std::is_object_address (this))
  {
    contract_assert<never_proven_conveyor_v> (std::is_object_address (t));
    t->f ();
  }

  void unproven ()
    pre<never_proven_conveyor_v> (std::is_object_address (this))
  { t->f (); } // { dg-error "cannot prove .is_object_address." }

  void stored (T *other)
    pre<never_proven_conveyor_v> (std::is_object_address (this))
    pre<never_proven_conveyor_v> (std::is_object_address (t))
  {
    t = other;
    t->f (); // { dg-error "cannot prove .is_object_address." }
  }
};
