// D4324: what the overflow guards don't prove.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <climits>

int one_more (int l, int r) conveyor
{ if (l > 0 && INT_MAX - l >= r) return l + r + 1; return 0; }	// { dg-error "not provably free of overflow" }

int reassigned (int l, int r) conveyor
{ if (l > 0 && INT_MAX - l >= r) { l = 7; r = INT_MAX; return l + r; } return 0; }	// { dg-error "not provably free of overflow" }

int either (int l, int r) conveyor
{ if (l > 0 || INT_MAX - l >= r) return l + r; return 0; }	// { dg-error "not provably free of overflow" }

int two_more (int l, int r) conveyor
{ if (l > 0 && INT_MAX - l > r) return l + r + 2; return 0; }	// { dg-error "not provably free of overflow" }

int other_operand (int l, int r, int z) conveyor
{ if (l > 0 && INT_MAX - l >= z) return l + r; return 0; }	// { dg-error "not provably free of overflow" }

/* The guard itself can overflow; assuming it doesn't makes 'l' non-negative,
   which the GIMPLE engine uses for 'l + r' and the AST engine doesn't.  */
int upper_only (int l, int r) conveyor
{ if (INT_MAX - l >= r) return l + r; return 0; }	// { dg-error "not provably free of overflow" }
