// D4324: -Wfunction-pointer-contract-mismatch's argument-passing check
// walks the callee's own DECL_ARGUMENTS (build_over_call's pre-existing
// 'parmd' walk) to find a parameter's own contract -- confirm this
// still works after the callee has been redeclared (a scenario flagged
// as needing direct verification when this check was designed, since
// DECL_ARGUMENTS can in principle be swapped across a redeclaration).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -Wfunction-pointer-contract-mismatch" }

#include <contracts>

int h (int x) pre<> (x > 1) { return x; }

void call_arg (int (*p) (int x) pre<> (x > 0));
void call_arg (int (*p) (int x) pre<> (x > 0));

void
call_after_redecl ()
{
  call_arg (h);			// { dg-warning "contract" }
}
