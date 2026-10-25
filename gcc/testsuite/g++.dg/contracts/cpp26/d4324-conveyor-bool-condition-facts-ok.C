// D4324: a call returning bool, tested by a condition or asserted,
// establishes facts: the call itself, for a precondition naming the same
// call, and what the callee's postconditions say of its arguments given
// the result ('if (ok (a, b))' with 'post (r: !r || ...)').  Neither
// engine learned anything from such a call, so all of these were
// rejected.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <climits>
#include <contracts>
using std::contracts::conveyor_assert_v;

bool ok_sum (const long long l, const long long r) conveyor
  post<conveyor_assert_v> (res: !res || (l > 0 ? r <= LLONG_MAX - l : r >= LLONG_MIN - l))
{ return (l > 0 && LLONG_MAX - l >= r) || (l <= 0 && LLONG_MIN - l <= r); }

bool small (const int x) conveyor post<conveyor_assert_v> (res: res == (x < 100));
bool tiny (const int x) conveyor;
void use_tiny (int x) conveyor pre<conveyor_assert_v> (tiny (x));
void use100 (int x) conveyor pre<conveyor_assert_v> (x < 100);

long long tested (long long l, long long r) conveyor
{ if (ok_sum (l, r)) return l + r; return 0; }

long long swapped (long long l, long long r) conveyor
{ if (ok_sum (r, l)) return l + r; return 0; }

long long asserted (long long l, long long r) conveyor
{ contract_assert<conveyor_assert_v> (ok_sum (l, r)); return l + r; }

long long required (const long long l, const long long r) conveyor
  pre<conveyor_assert_v> (ok_sum (l, r))
{ return l + r; }

int equality_post (int x) conveyor { if (small (x)) return x + 1; return 0; }
int negated (int x) conveyor { if (!small (x)) return 0; return x + 1; }
int else_branch (int x) conveyor { if (small (x)) return 0; return x - 1; }
void value_precondition (int x) conveyor { if (small (x)) use100 (x); }
void same_call (int x) conveyor { if (tiny (x)) use_tiny (x); }
