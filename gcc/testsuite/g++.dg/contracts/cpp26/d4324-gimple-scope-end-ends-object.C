// D4324 (GIMPLE engine): leaving a block ends its automatic variables'
// objects, and so the facts of pointers that designate them: a reference
// to an object whose scope has ended isn't an object's (P4445 BUG-23).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>

int value (const int &r) conveyor { return r; }

int a () { const int *p; { int x = 5; p = &x; } return value (*p); } // { dg-error "cannot prove 'is_object_address'" }
int b () { const int *p; int x = 5; { p = &x; } return value (*p); }
int c () { int s = 0; for (int i = 0; i < 3; ++i) { int x = i; s += value (x); } return s; }
int d () { const int *p; { int x = 5; p = &x; } { int y = 6; p = &y; return value (*p); } }
