// D4324: an outlined predicate sees a captured local itself, not a copy:
// its address, an array's address, and no unannounced class copy.  Every
// assertion holds; a spurious violation terminates.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

int copies;
struct S
{
  int a;
  S (int a) : a (a) {}
  S (const S &o) : a (o.a) { ++copies; }
  bool ok () const { return a == 2; }
};

int main ()
{
  int x = 1;
  int *p = &x;
  contract_assert (&x == p);
  wchar_t buf[4];
  wchar_t *q = buf;
  contract_assert (buf == q);
  int a[3] = { 1, 2, 3 };
  contract_assert (a[1] == 2 && &a[2] == a + 2);
  S s (2);
  S *sp = &s;
  contract_assert (&s == sp && s.ok ());
  const int &r = x;
  contract_assert (&r == p);
  if (copies != 0)
    __builtin_abort ();
}
