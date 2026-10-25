// D4324: d4324-conveyor-assert-range-composition.C under the GIMPLE
// engine.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>
using std::contracts::never_proven_conveyor_v;

long
bound_expr (long n) conveyor
{
  contract_assert<never_proven_conveyor_v>
    (n >= -(__PTRDIFF_MAX__ / 1024) && n <= __PTRDIFF_MAX__ / 1024);
  return n * 512;
}

long
comparison (long n, const int *p) conveyor
{
  contract_assert<never_proven_conveyor_v> (n >= -100 && n <= 100);
  return n - (p != nullptr) + bool (p);
}

long
product (long n, long b) conveyor
{
  const long m = n;
  contract_assert<never_proven_conveyor_v>
    (m >= -100 && m <= 100 && b > 0 && b <= 512);
  return b * (m - 1);
}

template<class T>
struct It
{
  T *cur, *first, *last;
  T **node;
  friend long operator- (const It &x, const It &y) noexcept conveyor
  {
    const long nodes = x.node - y.node;
    const long xpos = x.cur - x.first;
    const long ypos = y.last - y.cur;
    contract_assert<never_proven_conveyor_v>
      (nodes >= -(__PTRDIFF_MAX__ / 1024) && nodes <= __PTRDIFF_MAX__ / 1024
       && xpos >= 0 && xpos <= 512 && ypos >= 0 && ypos <= 512);
    return 512 * (nodes - bool (x.node)) + xpos + ypos;
  }
};
long use (It<int> a, It<int> b) { return a - b; }

long
too_wide (long n) conveyor
{
  contract_assert<never_proven_conveyor_v> (n >= -(__PTRDIFF_MAX__ / 256) && n <= 0);
  return n * 512;	// { dg-error "not provably free of overflow" }
}
