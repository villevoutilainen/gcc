// D4324: a trusted pointer value read through a CHAIN of pointer data
// members -- 'mobj->d', with or without 'mobj' itself trusted -- is proven
// for a later dereference of the same chain ('&mobj->d->v'), as the
// one-level 'this->d' and a local copy of 'mobj->d' are.  Writing a member
// along the chain (directly, or through another pointer to its object) in
// between forgets it.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=ast" }

#include <contracts>

#define TRUST(p) \
  contract_assert<std::contracts::never_proven_conveyor_v> (std::is_object_address (p))

struct V
{
  int n;
  int size () const pre<std::contracts::conveyor_assert_v> (std::is_object_address (this));
};
struct P { V v; };
struct O { P *d; };

struct B
{
  O *mobj;
  int idx;

  int one () const { TRUST (mobj->d); return int (mobj->d->v.size ()); }
  int two () const { TRUST (mobj); TRUST (mobj->d); return int (mobj->d->v.size ()); }
  int local () const { P *const d = mobj->d; TRUST (d); return int (d->v.size ()); }

  int none () const
  {
    return int (mobj->d->v.size ());	// { dg-error "cannot prove" }
  }

  int repointed (O *other)
  {
    TRUST (mobj->d);
    mobj = other;
    return int (mobj->d->v.size ());	// { dg-error "cannot prove" }
  }

  int rewritten (P *q)
  {
    TRUST (mobj->d);
    mobj->d = q;
    return int (mobj->d->v.size ());	// { dg-error "cannot prove" }
  }

  int aliased (P *q)
  {
    TRUST (mobj->d);
    O *o = mobj;
    o->d = q;
    return int (mobj->d->v.size ());	// { dg-error "cannot prove" }
  }

  int through_pointer (P *q)
  {
    TRUST (mobj->d);
    P **pp = &mobj->d;
    *pp = q;
    return int (mobj->d->v.size ());	// { dg-error "cannot prove" }
  }

  int through_this_copy (P *q)
  {
    TRUST (mobj->d);
    B *b = this;
    b->mobj->d = q;
    return int (mobj->d->v.size ());	// { dg-error "cannot prove" }
  }

  int in_loop (P *q, int n)
  {
    TRUST (mobj->d);
    int s = 0;
    for (int i = 0; i < n; ++i)
      {
	s += mobj->d->v.size ();	// { dg-error "cannot prove" }
	mobj->d = q;
      }
    return s;
  }
};

struct C { P *d; int f () const { TRUST (d); return int (d->v.size ()); } };
