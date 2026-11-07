// D4324: a conveyor function or predicate may call only the built-ins
// with no side effects and no unchecked undefined behavior, named one by
// one; GCC's attributes say what a built-in writes, not when it is
// undefined.  Other built-ins were accepted, unless -fno-builtin turned
// them into ordinary functions (P4445 BUG-09).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

void bad_printf () conveyor { __builtin_printf ("x"); } // { dg-error "not declared 'conveyor'" }
void* bad_malloc () conveyor { return __builtin_malloc (4); } // { dg-error "not declared 'conveyor'" }
void bad_exit () conveyor { __builtin_exit (0); } // { dg-error "not declared 'conveyor'" }
int bad_clz (unsigned x) conveyor { return __builtin_clz (x); } // { dg-error "not declared 'conveyor'" }
bool bad_pred (int x) pre<std::contracts::conveyor_assert_v> (__builtin_printf ("x") > 0); // { dg-error "not declared 'conveyor'" }

int ok_pop (unsigned x) conveyor { return __builtin_popcount (x); }
bool ok_ovf (int a, int b) conveyor { int r = 0; return __builtin_add_overflow (a, b, &r); }
bool ok_nan (double d) conveyor { return __builtin_isnan (d) || __builtin_isinf (d); }
unsigned ok_bswap (unsigned x) conveyor { return __builtin_bswap32 (x); }
bool ok_ce () conveyor { return __builtin_is_constant_evaluated (); }
int ok_expect (int x) conveyor { if (__builtin_expect (x > 0, 1)) return 1; __builtin_unreachable (); }
int* ok_aligned (int* p) conveyor { return static_cast<int*> (__builtin_assume_aligned (p, alignof (int))); }
