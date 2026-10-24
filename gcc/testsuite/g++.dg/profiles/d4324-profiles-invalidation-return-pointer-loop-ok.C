// P3446R0/P4296R0 Invalidation profile: a pointer parameter returned
// after a loop that leaves it unchanged, or swaps it around.  The
// loop's PHIs reach themselves, and following them went on to the
// recursion limit, which denies.
// { dg-do compile { target c++17 } }

[[profiles::enforce(std::invalidation)]];

int *
unchanged (int *p, int n)
{
  for (int i = 0; i < n; ++i)
    p = p;
  return p;
}

int *
swapped (int *p, int *q, int n)
{
  for (int i = 0; i < n; ++i)
    {
      int *t = p;
      p = q;
      q = t;
    }
  return p;
}
