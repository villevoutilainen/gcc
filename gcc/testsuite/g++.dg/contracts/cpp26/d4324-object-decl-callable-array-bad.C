// D4324: declaration-level contracts on callable-typed object
// declarations are restricted to a declaration that is *itself*
// directly callable -- an array of function pointers is not itself
// callable (you must index into it first), so attaching a clause there
// is rejected rather than silently dropped.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fsyntax-only" }

#include <contracts>

void (*arr[3]) (int x) pre<> (x > 0); // { dg-error "contract specifier is only valid on a callable-typed declaration" }

void f (void (*p[3]) (int x) pre<> (x > 0)); // { dg-error "contract specifier is only valid on a callable-typed declaration" }
