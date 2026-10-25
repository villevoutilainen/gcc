// D4324: a compound divisor whose range includes zero.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int reaches_zero (int i) conveyor
{ if (i >= 0 && i < 100) return 100 / (i - 1); return 0; }	// { dg-error "divisor" }

int starts_below (int i) conveyor
{ if (i >= -1 && i < 100) return 100 / (i + 1); return 0; }	// { dg-error "divisor" }

/* An unsigned sum can wrap to zero.  */
unsigned wraps (unsigned u) conveyor
{ if (u >= 1) return 100u / (u + 1u); return 0; }	// { dg-error "divisor" }
