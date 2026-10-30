// D4324: a trusted is_object_address fact about a pointer VALUE read
// through a reference to it ('T *&r'), or from an element of a local
// array of pointers ('a[i]'), holds for the reads that follow -- until
// that pointer may have changed: a store through the reference, to the
// element (or any element, at an unknown index), to the index, or to the
// pointer a local reference is bound to, or a call given a pointer that
// may reach it.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=ast" }

#include <contracts>

struct F { void del (int *) pre<std::contracts::conveyor_assert_v> (std::is_object_address (this)) {} };
#define TRUST(X) \
  contract_assert<std::contracts::never_proven_conveyor_v> (std::is_object_address (X))
void reseat (F *&);
typedef F *Fp;

void
good (int *p, F *&d, F *pool[4], int k, Fp b)
{
  TRUST (d);
  d->del (p);
  F *l[4] = { pool[0], pool[1], pool[2], pool[3] };
  TRUST (l[2]);
  l[2]->del (p);
  TRUST (l[k]);
  l[k]->del (p);
  Fp m = b;
  const Fp &a = m;
  TRUST (a);
  a->del (p);
}

struct P { int a; };
int use1 (const P &) pre<std::contracts::conveyor_assert_v> (true);
int use2 (const P &) pre<std::contracts::conveyor_assert_v> (true);
typedef const P *It;

// A call given only the pointer's value can't change the pointer.
int
two_uses (It b)
{
  It m = b;
  const It &a = m;
  TRUST (a);
  return use1 (*a) + use2 (*a);
}

void r1 (int *p, F *&d, F *o) { TRUST (d); d = o; d->del (p); }	// { dg-error "cannot prove" }
void r2 (int *p, F *&d) { TRUST (d); reseat (d); d->del (p); }	// { dg-error "cannot prove" }

void
a1 (int *p, F *pool[4], F *o)
{
  F *l[4] = { pool[0], pool[1], pool[2], pool[3] };
  TRUST (l[2]);
  l[2] = o;
  l[2]->del (p);	// { dg-error "cannot prove" }
}

void
a2 (int *p, F *pool[4], int k)
{
  F *l[4] = { pool[0], pool[1], pool[2], pool[3] };
  TRUST (l[k]);
  ++k;
  l[k]->del (p);	// { dg-error "cannot prove" }
}

void
a3 (int *p, F *pool[4], int k, int j, F *o)
{
  F *l[4] = { pool[0], pool[1], pool[2], pool[3] };
  TRUST (l[k]);
  l[j] = o;
  l[k]->del (p);	// { dg-error "cannot prove" }
}

void
c1 (int *p, Fp b, Fp o)
{
  Fp m = b;
  const Fp &a = m;
  TRUST (a);
  m = o;
  a->del (p);	// { dg-error "cannot prove" }
}
