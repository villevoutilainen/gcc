// D4324 (GIMPLE engine): clz and ctz of zero (but for the generic forms'
// fallback) and the absolute value of a type's minimum are undefined: a
// conveyor function may call these built-ins only with the argument proven
// safe.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>

int clz_bad (unsigned x) conveyor { return __builtin_clz (x); }                    // { dg-error "not provably nonzero" }
int clz_ok (unsigned x) conveyor { if (x == 0) return 32; return __builtin_clz (x); }
int ctz_bad (unsigned long x) conveyor { return __builtin_ctzl (x); }              // { dg-error "not provably nonzero" }
int ctz_ok (unsigned long x) conveyor { return x ? __builtin_ctzl (x) : 64; }
int clzg_fallback (unsigned x) conveyor { return __builtin_clzg (x, 32); }
int clzg_bad (unsigned x) conveyor { return __builtin_clzg (x); }                   // { dg-error "not provably nonzero" }
int abs_bad (int x) conveyor { return __builtin_abs (x); }                          // { dg-error "absolute value of .x. not provably representable" }
int abs_ok (int x) conveyor { if (x == -__INT_MAX__ - 1) return 0; return __builtin_abs (x); }
int abs_pre (int x) conveyor pre<std::contracts::proven_conveyor_v> (x > -100) { return __builtin_abs (x); }
long labs_bad (long x) conveyor { return __builtin_labs (x); }                      // { dg-error "absolute value of .x. not provably representable" }
// A count of bits is in range: adding to it doesn't overflow.
int sum (unsigned x) conveyor { if (x == 0) return 0; return 1 + __builtin_clz (x); }
int folded () conveyor { return __builtin_clz (8u) + __builtin_abs (-5); }
