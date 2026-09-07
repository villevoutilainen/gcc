// P3446R0/P4296R0 Invalidation profile: a function taking a plain,
// non-owner-accepting pointer parameter cannot invalidate that
// pointer -- unlike CE2's own owner-accepting-parameter case (see the
// companion -bad test), an ordinary "peek" call has no license to
// consume anything, so reading through an earlier alias afterward
// stays safe.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void peek (int *p);

int use ()
{
  int *p [[owner]] = new int (7);
  int *q = p;
  peek (p);
  int v = *q;
  delete p;
  return v;
}
