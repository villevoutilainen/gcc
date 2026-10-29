// D4324: a trusted element of an array reached through a POINTER DATA
// MEMBER -- 'd_p[i]', 'd_p[2]', and a pointer member of an anonymous union
// -- is proven for a later use of the same element, as one reached through
// a local pointer is.  Writing the member (or calling something that may)
// in between forgets it.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=ast" }

#include <contracts>

struct P
{
  long a[9];
  void f () pre<std::contracts::conveyor_assert_v> (std::is_object_address (this));
};

struct M
{
  P *d_p;
  bool d_created;
  union { P *d_alt_p; P *d_pool_p; };

  void deref () pre<std::contracts::conveyor_assert_v> (std::is_object_address (this))
  {
    contract_assert<std::contracts::never_proven_conveyor_v>
      (std::is_object_address (d_p));
    d_p->f ();
  }

  void index (int i) pre<std::contracts::conveyor_assert_v> (std::is_object_address (this))
  {
    contract_assert<std::contracts::never_proven_conveyor_v>
      (std::is_object_address (&d_p[i]));
    d_p[i].f ();
  }

  void constant () pre<std::contracts::conveyor_assert_v> (std::is_object_address (this))
  {
    contract_assert<std::contracts::never_proven_conveyor_v>
      (std::is_object_address (&d_p[2]));
    d_p[2].f ();
  }

  void anon () pre<std::contracts::conveyor_assert_v> (std::is_object_address (this))
  {
    if (d_created)
      {
	contract_assert<std::contracts::never_proven_conveyor_v>
	  (std::is_object_address (d_pool_p));
	d_pool_p->f ();
      }
  }

  void rewritten (P *q, int i)
    pre<std::contracts::conveyor_assert_v> (std::is_object_address (this))
  {
    contract_assert<std::contracts::never_proven_conveyor_v>
      (std::is_object_address (&d_p[i]));
    d_p = q;
    d_p[i].f ();	// { dg-error "cannot prove" }
  }

  void other (int i, int j)
    pre<std::contracts::conveyor_assert_v> (std::is_object_address (this))
  {
    contract_assert<std::contracts::never_proven_conveyor_v>
      (std::is_object_address (&d_p[i]));
    d_p[j].f ();	// { dg-error "cannot prove" }
  }
};
