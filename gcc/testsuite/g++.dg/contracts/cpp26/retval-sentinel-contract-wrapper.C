// A function with contracts returning a class with a non-trivial destructor
// from more than one local needs the return-value sentinel.  The contract
// wrapper around the function body must not declare it a second time
// (ICE in gimple_add_tmp_var), nor destroy the return value twice when a
// destructor throws.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts" }

int live, thr;

struct D
{
  int v;
  D () : v (0) { ++live; }
  D (const D &o) : v (o.v) { ++live; }
  ~D () noexcept (false) { --live; if (thr && v == 7) { thr = 0; throw 1; } }
};

bool f (int i) { return i & 1; }

D
pre_only (const D &i, int k) pre (i.v >= 0)
{
  D cur = i;
  if (f (k))
    return cur;
  D other = i;
  other.v = 7;
  if (f (k >> 1))
    return other;
  return cur;
}

D
pre_post (const D &i, int k) pre (i.v >= 0) post (r: r.v >= 0)
{
  D cur = i;
  if (f (k))
    return cur;
  D other = i;
  other.v = 7;
  if (f (k >> 1))
    return other;
  return cur;
}

int
main ()
{
  D a;
  for (int k = 0; k < 4; ++k)
    {
      D r1 = pre_only (a, k);
      D r2 = pre_post (a, k);
    }
  if (live != 1)
    __builtin_abort ();

  /* 'other' throws from its destructor while 'cur' is being returned.  */
  thr = 1;
  try { D r = pre_only (a, 0); } catch (int) {}
  if (live != 1)
    __builtin_abort ();
  thr = 1;
  try { D r = pre_post (a, 0); } catch (int) {}
  if (live != 1)
    __builtin_abort ();
}
