// D4324 (GIMPLE engine): a call that isn't conveyor may write an object of
// static storage duration, or a local whose address has been handed out
// earlier, though it isn't given a route to it: what was known of its value
// ends (P4445 BUG-17).  A local's address is handed out by a call given a
// pointer to it or a non-const reference to it, or by storing its address
// anywhere but in a local variable; not, for now, by a member call on it.
// A local whose address is never handed out keeps its facts.  The GIMPLE
// engine treats any call as writing global memory, a conveyor call too.
// An immediately invoked lambda writing a capture by reference changes it.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>

int divide (int a, int b) conveyor
  pre<std::contracts::proven_conveyor_v>(b != 0) { return a / b; }
int nz (int x) conveyor
  post<std::contracts::conveyor_assert_v>(r: r != 0) { return x == 0 ? 1 : x; }

int gy;
int *gp;
struct H { int *p; };
void clear ();
void keep (int *);
void keepc (const int *);
void keepr (int &);
int touch (int v) conveyor { return v; }

int c (int x) { gy = nz (x); clear (); return divide (10, gy); } // { dg-error "cannot prove that 'gy' is nonzero" }
int e (int x) { int y = nz (x); keep (&y); clear (); return divide (10, y); } // { dg-error "cannot prove that 'y' is nonzero" }
int er (int x) { int y = nz (x); keepr (y); clear (); return divide (10, y); } // { dg-error "cannot prove that 'y' is nonzero" }
int ep (int x) { int y = nz (x); int *q = &y; keep (q); clear (); return divide (10, y); } // { dg-error "cannot prove that 'y' is nonzero" }
int es (int x) { int y = nz (x); gp = &y; clear (); return divide (10, y); } // { dg-error "cannot prove that 'y' is nonzero" }
int em (int x, H &h) { int y = nz (x); h.p = &y; clear (); return divide (10, y); } // { dg-error "cannot prove that 'y' is nonzero" }
int ec (int x) { int y = nz (x); keepc (&y); clear (); return divide (10, y); } // { dg-error "cannot prove that 'y' is nonzero" }
int l (int x) { int y = nz (x); clear (); return divide (10, y); }
int v (int x) { gy = nz (x); touch (1); return divide (10, gy); } // { dg-error "cannot prove that 'gy' is nonzero" }

int w () conveyor { int x = 5; [&] () { x = 0; } (); return 10 / x; } // { dg-error "divisor .x. not provably nonzero" }
int k () conveyor { int x = 5; return [&] () { return 10 / x; } (); }
