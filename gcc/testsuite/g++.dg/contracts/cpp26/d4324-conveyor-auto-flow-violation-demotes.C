// D4324: conveyor(auto) deduction checks every conveyor rule, including
// the ones checked by analyzing the body (overflow, divisor, dereference,
// array bounds), not just the ones checked while it is parsed.  A
// violation of any of them makes the specialization an ordinary
// function instead of being a hard error; it is then only rejected where
// a conveyor function or predicate calls it.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

namespace sc = std::contracts;

template <class T> T twice (T x) conveyor(auto) { return x + x; }
template <class T> T quot (T a, T b) conveyor(auto) { return a / b; }
template <class T> T load (T* p) conveyor(auto) { T v = *p; return v; }
template <class T> T at (int i) conveyor(auto) { T a[3] = {1, 2, 3}; T v = a[i]; return v; }
template <class T> T safe (T x) conveyor(auto) { return x < 0 ? 0 : x; }
template <class T> T outer (T x) conveyor(auto) { T r = twice (x); return r; }
template <class T> T rec (T x) conveyor(auto) { if (x < 0) return 0; T r = rec<T> (x / 2 - 1); return r; }
template <class T> bool pos (T x) conveyor(auto) { return x > 0; }

// The first deduction may come from a conveyor caller.
int strict_first (int x) conveyor { int r = twice (x); return r; } // { dg-error "call to .T twice\\(T\\) \\\[with T = int\\\]., which is not declared .conveyor." }

int ordinary (int x, int* p)
{ return twice (x) + quot (x, 2) + load (p) + at<int> (x) + safe (x) + outer (x) + rec (x); }

int strict_ok (int x) conveyor { return safe (x); }
int strict_quot (int x) conveyor { int r = quot (x, 2); return r; } // { dg-error "which is not declared .conveyor." }
int strict_load (int* p) conveyor { int r = load (p); return r; }   // { dg-error "which is not declared .conveyor." }
int strict_at (int i) conveyor { int r = at<int> (i); return r; }   // { dg-error "which is not declared .conveyor." }
int strict_outer (int x) conveyor { int r = outer (x); return r; }  // { dg-error "which is not declared .conveyor." }

int pred_ok (int x) pre<sc::conveyor_assert_v> (pos (x));
int pred_bad (int x) pre<sc::conveyor_assert_v> (twice (x) > 0);   // { dg-error "which is not declared .conveyor." }

// Outside a deduction, the same violations are still errors.
int plain_bad (int x, int y) conveyor { int q = x / y; return q; } // { dg-error "divisor .y. not provably nonzero" }
