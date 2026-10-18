// D4324: an error in the operand of a return statement in a conveyor
// function used to also produce "conveyor function ... must contain a
// 'return' statement", since the erroneous return statement is dropped.
// Only the real error is reported now; a genuinely missing return is
// still diagnosed.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

int helper (int x) { return x; }

int calls (int x) conveyor { return helper (x); }	// { dg-error "which is not declared .conveyor." }
int fresh () conveyor { return *new int (1); }		// { dg-error ".new.-expression not permitted" }
int partial (int x) conveyor
{
  if (x)
    return helper (x);					// { dg-error "which is not declared .conveyor." }
  else
    return 1;
}
int missing (int x) conveyor { if (x) return 1; }	// { dg-error "must contain a .return. statement" }
// { dg-warning "control reaches end of non-void function" "" { target *-*-* } .-1 }
int fine (int x) conveyor { return x < 0 ? 0 : x; }
