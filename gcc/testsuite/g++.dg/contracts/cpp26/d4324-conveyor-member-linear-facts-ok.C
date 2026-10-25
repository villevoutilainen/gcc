// D4324: overflow guards and the facts a postcondition states can bound
// sums of members and of values a linear form doesn't express, through
// what was stored.  After 'contract_assert (valid (seconds, n))', whose
// postcondition bounds 'seconds + n / k', and 'd_seconds = seconds;',
// 'd_seconds += n / k' can't overflow; nor can 'd_seconds + seconds'
// after 'ok (d_seconds, seconds)'.  The bounds named only plain
// variables, and neither engine followed a store into a member.  A
// constant static member ('k') is also a nonzero divisor.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <climits>
#include <contracts>
using std::contracts::conveyor_assert_v;
using std::contracts::never_proven_conveyor_v;

struct TI
{
  long long d_seconds;
  int d_nanoseconds;
  static constexpr int k = 1000000000;

  static bool ok (const long long l, const long long r) conveyor
    post<conveyor_assert_v> (res: !res || (l > 0 ? r <= LLONG_MAX - l
						  : r >= LLONG_MIN - l));
  static bool valid (const long long s, const int n) conveyor
    post<conveyor_assert_v> (res: !res || (s > 0 ? n / k <= LLONG_MAX - s
						  : n / k >= LLONG_MIN - s));

  void add (long long seconds)
  {
    contract_assert<never_proven_conveyor_v> (std::is_object_address (this));
    contract_assert<conveyor_assert_v> (ok (d_seconds, seconds));
    contract_assert<conveyor_assert_v> (ok (d_seconds + seconds, 0));
  }

  void set (long long seconds, int nanoseconds) conveyor
  {
    contract_assert<conveyor_assert_v> (valid (seconds, nanoseconds));
    d_seconds = seconds;
    if (nanoseconds >= k || nanoseconds <= -k)
      {
	d_seconds += nanoseconds / k;
	d_nanoseconds = nanoseconds % k;
      }
    else
      d_nanoseconds = nanoseconds;
  }

  long long direct (long long seconds, int nanoseconds) conveyor
  {
    contract_assert<conveyor_assert_v> (valid (seconds, nanoseconds));
    return seconds + nanoseconds / k;
  }

  long long local (long long seconds, int nanoseconds) conveyor
  {
    contract_assert<conveyor_assert_v> (valid (seconds, nanoseconds));
    long long d = seconds;
    d += nanoseconds / k;
    return d;
  }
};

/* The constant an enumerator, converted from its enumeration type.  */
struct TE
{
  long long d_seconds;
  enum { k = 1000000000 };
  static bool valid (const long long s, const int n) conveyor
    post<conveyor_assert_v> (res: !res || (s > 0 ? n / k <= LLONG_MAX - s
						  : n / k >= LLONG_MIN - s));
  void set (long long seconds, int nanoseconds) conveyor
  {
    contract_assert<conveyor_assert_v> (valid (seconds, nanoseconds));
    d_seconds = seconds;
    d_seconds += nanoseconds / k;
  }
};
