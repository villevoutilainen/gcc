// D4324: a non-conveyor call handed a parameter, 'this' included, or a
// parameter that may alias it, may end its object, so its self-trusted
// is_object_address doesn't hold after the call, in both engines;
// a postcondition or contract_assert stating it makes it hold again.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct H
{
  int v;
  void cm () conveyor {}
  void nm ();
};
void touch (H&);
void renew (H& r) post<sc::conveyor_assert_v> (std::is_object_address (&r));
void look (const H&);

void by_call (H& r) pre<sc::conveyor_assert_v> (std::is_object_address (&r))
{
  touch (r);
  r.cm (); // { dg-error "cannot prove .is_object_address." }
}

void by_member (H& r) pre<sc::conveyor_assert_v> (std::is_object_address (&r))
{
  r.nm ();
  r.cm (); // { dg-error "cannot prove .is_object_address." }
}

void by_alias (H& r, H& s) pre<sc::conveyor_assert_v> (std::is_object_address (&r))
{
  touch (s);
  r.cm (); // { dg-error "cannot prove .is_object_address." }
}

void const_ref_keeps (H& r) pre<sc::conveyor_assert_v> (std::is_object_address (&r))
{
  look (r);
  r.cm ();
}

void post_renews (H& r) pre<sc::conveyor_assert_v> (std::is_object_address (&r))
{
  renew (r);
  r.cm ();
}

void assert_renews (H& r) pre<sc::conveyor_assert_v> (std::is_object_address (&r))
{
  r.nm ();
  contract_assert<sc::never_proven_conveyor_v> (std::is_object_address (&r));
  r.cm ();
}

void one_branch (H& r, bool b) pre<sc::conveyor_assert_v> (std::is_object_address (&r))
{
  if (b)
    touch (r);
  r.cm (); // { dg-error "cannot prove .is_object_address." }
}

struct S
{
  H h;
  void get ();
  S () { get (); h.cm (); } // { dg-error "cannot prove .is_object_address." }
};
