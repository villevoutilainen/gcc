// D4324/P2680 item 8: a division/modulo whose divisor is a local
// variable initialized from an OPAQUE function call (declared, never
// defined -- genuinely invisible to this translation unit, unlike an
// IILE's own visible closure body, or a conveyor accessor's own
// declared postcondition) must still be rejected, exactly like an
// ordinary unprovable parameter (d4324-conveyor-divmod-nonzero-bad.C)
// -- confirms the analysis doesn't silently, unsoundly trust an
// opaque call's result just because it's bound to a named local
// rather than used directly.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int get_divisor () conveyor;

int f (int a) conveyor
{
  int b = get_divisor ();
  return a / b; // { dg-error "divisor .b. not provably nonzero in a conveyor function" }
}

int g (int a) conveyor
{
  int b = get_divisor ();
  return a % b; // { dg-error "divisor .b. not provably nonzero in a conveyor function" }
}

int main () { return 0; }
