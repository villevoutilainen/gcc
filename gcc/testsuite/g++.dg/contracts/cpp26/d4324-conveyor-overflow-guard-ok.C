// D4324: the usual overflow guards prove the sum or difference they
// guard.  'INT_MAX - a >= b' bounds 'a + b' by INT_MAX, and 'a > 0'
// bounds it from below; the two cases of 'a > 0 ? ... : ...' each bound
// one side, and together both.  Neither engine related the guard to
// 'a + b', so every one of these was rejected.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <climits>

long long one_case (long long l, long long r) conveyor
{ if (l > 0 && LLONG_MAX - l >= r) return l + r; return 0; }

long long both_cases (long long l, long long r) conveyor
{ if ((l > 0 && LLONG_MAX - l >= r) || (l <= 0 && LLONG_MIN - l <= r)) return l + r; return 0; }

long long choice (long long l, long long r) conveyor
{ if (l > 0 ? LLONG_MAX - l >= r : LLONG_MIN - l <= r) return l + r; return 0; }

int difference (int a, int b) conveyor
{ if (b >= 0 ? a >= INT_MIN + b : a <= INT_MAX + b) return a - b; return 0; }

int strict (int l, int r) conveyor
{ if (l > 0 && INT_MAX - l > r) return l + r + 1; return 0; }

int other_order (int l, int r) conveyor
{ if (l > 0 && r <= INT_MAX - l) return r + l; return 0; }
