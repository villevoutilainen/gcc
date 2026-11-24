// D4324: the GIMPLE engine's diagnostics of what a call owes its callee
// note where the callee is declared, after their other notes, as the AST
// engine's do.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
using namespace std::contracts;

int div (int a, int b) conveyor // { dg-message "declared here" }
  pre<proven_conveyor_v> (b > 0) pre<proven_conveyor_v> (a >= 0)
{ return a / b; }
int g (int* p) conveyor // { dg-message "declared here" }
  pre<conveyor_assert_v> (std::is_object_address (p))
{ return *p; }

int f1 (int x) conveyor { return div (1, x); } // { dg-error "cannot prove that 'x' satisfies the precondition" }
int f2 () conveyor { return div (1, -1); } // { dg-error "provably violates" }
int f3 (int* q) conveyor { return g (q); } // { dg-error "cannot prove 'is_object_address' for 'q'" }
