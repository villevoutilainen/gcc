// D4324: a contract_assert naming a control object in a constructor's body
// is evaluated through an outlined predicate taking the constructor's
// parameters.  The constructor's 'this' carries "clobber *this"
// (-flifetime-dse=2: *this is uninitialized on entry), and the predicate
// mustn't inherit it: it runs after the members are initialized, and
// -Wuninitialized called each member it read uninitialized.  (Seen with
// libc++'s hardening assertions in iota_view, repeat_view, flat_map and
// __bit_iterator constructors.)
// { dg-do run { target c++26 } }
// { dg-options "-fcontracts -fcontract-control-objects -Wall -O2" }

#include <contracts>

struct S
{
  int b;
  explicit S (int x) : b (x)
  {
    contract_assert<std::contracts::noexcept_assert_v> (b >= 0); // { dg-bogus "uninitialized" }
  }
};

struct T
{
  int lo, hi;
  T (int l, int h) : lo (l), hi (h)
  {
    contract_assert<std::contracts::noexcept_assert_v> (lo <= hi); // { dg-bogus "uninitialized" }
  }
};

int
main ()
{
  S s (1);
  T t (1, 2);
  return s.b + t.lo - 2;
}
