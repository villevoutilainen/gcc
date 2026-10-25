// D4324: what a bound on 'seconds + n / k' doesn't say once the member
// or local holding 'seconds' has changed, a call may have changed it, it
// holds something else, or the term differs.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <climits>
#include <contracts>
using std::contracts::conveyor_assert_v;

struct TI
{
  long long d_seconds;
  static constexpr int k = 1000000000;
  static bool valid (const long long s, const int n) conveyor
    post<conveyor_assert_v> (res: !res || (s > 0 ? n / k <= LLONG_MAX - s
						  : n / k >= LLONG_MIN - s));
  void touch () conveyor;

  void overwritten (long long s, int n, long long o) conveyor
  {
    contract_assert<conveyor_assert_v> (valid (s, n));
    d_seconds = s;
    d_seconds = o;
    d_seconds += n / k;	// { dg-error "not provably free of overflow" }
  }

  void called (long long s, int n) conveyor
  {
    contract_assert<conveyor_assert_v> (valid (s, n));
    d_seconds = s;
    touch ();
    d_seconds += n / k;	// { dg-error "not provably free of overflow" }
  }

  void shifted (long long s, int n) conveyor
  {
    contract_assert<conveyor_assert_v> (valid (s, n));
    d_seconds = s + 1;	// { dg-error "not provably free of overflow" }
    d_seconds += n / k;	// { dg-error "not provably free of overflow" }
  }

  long long local (long long s, int n, long long t) conveyor
  {
    contract_assert<conveyor_assert_v> (valid (s, n));
    long long d = s;
    d = t;
    d += n / k;	// { dg-error "not provably free of overflow" }
    return d;
  }

  void other (TI &o, long long s, int n) conveyor
  {
    contract_assert<conveyor_assert_v> (valid (s, n));
    o.d_seconds = s;
    d_seconds += n / k;	// { dg-error "not provably free of overflow" }
  }

  long long divisor (long long s, int n) conveyor
  {
    contract_assert<conveyor_assert_v> (valid (s, n));
    return s + n / 3;	// { dg-error "not provably free of overflow" }
  }
};
