// D4324: a trusted element '&a[i]' of an array reached through a
// REFERENCE-TO-ARRAY parameter is proven for a later use of the same
// element, as one reached through a pointer parameter is -- also with a
// loop variable as the index.  Changing the index in between, or using
// another element, is not proven.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>

#define TRUST(p) \
  contract_assert<std::contracts::never_proven_conveyor_v> \
    (std::is_object_address (p))

struct E
{
  unsigned v[2];
  // Conveyor, so calling it doesn't end the element (the lifetime rule);
  // operator[] isn't, so each use below comes after the size () calls.
  unsigned size () const conveyor
    pre<std::contracts::conveyor_assert_v> (std::is_object_address (this))
  { return 2; }
  const unsigned &operator[] (int n) const
    pre<std::contracts::conveyor_assert_v> (std::is_object_address (this))
  { return v[n & 1]; }
};

template <int N> struct CI
{
  unsigned out[2 * N];
  constexpr CI (const E (&infos)[N])
  {
    for (int i = 0; i < N; ++i)
      {
	TRUST (&infos[i]);
	out[2 * i + 1] = infos[i].size ();
	out[2 * i] = infos[i][0];
      }
  }
};

unsigned g (const E (&a)[3]) { CI<3> c (a); return c.out[0]; }

unsigned h (const E (&a)[3], int i) { TRUST (&a[i]); return a[i][0]; }

unsigned k (const E *a, int i) { TRUST (&a[i]); return a[i][0]; }

unsigned hc (const E (&a)[3]) { TRUST (&a[2]); return a[2].size (); }

unsigned hu (E (&a)[4], int i)
{
  TRUST (&a[i]);
  unsigned s = a[i].size ();
  return s + a[i][1];
}

// A loop over a reference-to-array in a non-template function and in a
// non-template constructor: the index is a local, not a parameter.
unsigned loop (const E (&a)[3])
{
  unsigned s = 0;
  for (int i = 0; i < 3; ++i)
    {
      TRUST (&a[i]);
      s += a[i][0];
    }
  return s;
}

struct NC
{
  unsigned out[3];
  NC (const E (&infos)[3])
  {
    for (int i = 0; i < 3; ++i)
      {
	TRUST (&infos[i]);
	out[i] = infos[i].size ();
      }
  }
};

unsigned local (const E (&a)[3], int i)
{
  const E (&r)[3] = a;
  TRUST (&r[i]);
  return r[i][0];
}

unsigned none (const E (&a)[3], int i)
{
  return a[i][0];	// { dg-error "cannot prove .is_object_address. for .& a\\\[i\\\]." }
}

unsigned incremented (const E (&a)[3], int i)
{
  TRUST (&a[i]);
  ++i;
  return a[i][0];	// { dg-error "cannot prove" }
}

unsigned reassigned (const E (&a)[3], int i, int j)
{
  TRUST (&a[i]);
  i = j;
  return a[i][0];	// { dg-error "cannot prove" }
}

unsigned other (const E (&a)[3])
{
  TRUST (&a[0]);
  return a[1][0];	// { dg-error "cannot prove .is_object_address. for .& a\\\[1\\\]." }
}
