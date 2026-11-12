// D4324: a loop's accumulator is bounded only by a trip count the
// loop's counter, start, step and bound prove; an accumulator that would
// overflow within it, or a loop whose count isn't known, is rejected.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

// Each overflows, or may: a loop bound only helps when it is known and
// small enough.
int three () conveyor
{ int r = 0; for (int i = 0; i < 3; ++i) r += 1000000000; return r; } // { dg-error "not provably free of overflow" }
int fact13 () conveyor
{ int r = 1; for (int i = 2; i <= 13; ++i) r *= i; return r; } // { dg-error "not provably free of overflow" }
int unbounded (int n) conveyor
{ int t = 0; for (int i = 0; i < n; ++i) t += 1; return t; } // { dg-error "not provably free of overflow" }
int ne (int) conveyor
{ int t = 0; for (int i = 0; i != 60; ++i) t += 40000000; return t; } // { dg-error "not provably free of overflow" }
// U starts past 3 and wraps around to it: four billion iterations.
int wraps () conveyor
{ int t = 0; for (unsigned u = 5; u != 3; ++u) t += 1; return t; } // { dg-error "not provably free of overflow" }
// The body writes the counter: the loop may run forever.
int written (int k) conveyor
{ int t = 0; for (int i = 0; i < 3; ++i) { t += 1000000000; if (k) i -= 1; } return t; } // { dg-error "not provably free of overflow" }
int step2 () conveyor
{ int t = 0; for (int i = 0; i < 7; i += 2) t += 700000000; return t; } // { dg-error "not provably free of overflow" }
int cond () conveyor
{ int c = 0; for (int i = 0; i < 3; ++i) if (i % 3 == 0) c += 1000000000; else c += 1; return c; } // { dg-error "not provably free of overflow" }
int doubling () conveyor
{ int t = 1; for (int i = 0; i < 40; ++i) t += t; return t; } // { dg-error "not provably free of overflow" }
int negative () conveyor
{ int t = 0; for (int i = 0; i < 3; ++i) t -= 1000000000; return t; } // { dg-error "not provably free of overflow" }
int after () conveyor
{ int t = 0; for (int i = 0; i < 3; ++i) t += 700000000; return t + 100000000; } // { dg-error "not provably free of overflow" }
int brk (int k) conveyor
{ int t = 0; for (int i = 0; i < 10; ++i) { if (i == k) break; t += 200000000; } return t * 2; } // { dg-error "not provably free of overflow" }
int cont (int k) conveyor
{ int t = 0; for (int i = 0; i < 5; ++i) { if (i == k) continue; t += 500000000; } return t; } // { dg-error "not provably free of overflow" }
int exit_value () conveyor
{ int i = 0; for (; i < 11; ++i) {} return i * 200000000; } // { dg-error "not provably free of overflow" }
int nested () conveyor
{ int t = 0; for (int i = 0; i < 4; ++i) for (int j = 0; j < 3; ++j) t += 200000000; return t; } // { dg-error "not provably free of overflow" }
int nested4 () conveyor
{ int t = 0; for (int i = 0; i < 4; ++i) for (int j = 0; j < 3; ++j) t += 200000000; return t; } // { dg-error "not provably free of overflow" }
int mixed () conveyor
{ int t = 0; for (int i = 0; i < 10; ++i) { t += i; for (int j = 0; j < 10; ++j) t += 30000000; } return t; } // { dg-error "not provably free of overflow" }
int inner_unbounded (int n) conveyor
{ int t = 0; for (int i = 0; i < 3; ++i) for (int j = 0; j < n; ++j) t += 1; return t; } // { dg-error "not provably free of overflow" }
int deep () conveyor
{ int t = 0; for (int i = 0; i < 5; ++i) for (int j = 0; j < 4; ++j) for (int k = 0; k < 4; ++k) t += 30000000; return t; } // { dg-error "not provably free of overflow" }
int while_three () conveyor
{ int r = 0, i = 0; while (i < 3) { r += 1000000000; ++i; } return r; } // { dg-error "not provably free of overflow" }
// A do-while loop runs three times here, not two.
int dowhile () conveyor
{ int r = 0, i = 0; do { r += 1000000000; ++i; } while (i < 3); return r; } // { dg-error "not provably free of overflow" }
// The continue statement skips the increment: the loop may not end.
int while_continue (int k) conveyor
{ int r = 0, i = 0; while (i < 2) { if (k) continue; r += 1000000000; ++i; } return r; } // { dg-error "not provably free of overflow" }
// The increment isn't the only write.
int while_reset (int k) conveyor
{ int r = 0, i = 0; while (i < 2) { ++i; if (k) i = 0; r += 1000000000; } return r; } // { dg-error "not provably free of overflow" }
int dowhile_once () conveyor
{ int r = 0, i = 5; do { r += 1000000000; ++i; } while (i < 3); return r + 1200000000; } // { dg-error "not provably free of overflow" }
int while_after () conveyor
{ int t = 0, i = 0; while (i < 3) { t += 700000000; ++i; } return t + 100000000; } // { dg-error "not provably free of overflow" }
int dowhile_after () conveyor
{ int t = 0, i = 0; do { t += 700000000; ++i; } while (i < 3); return t + 100000000; } // { dg-error "not provably free of overflow" }
