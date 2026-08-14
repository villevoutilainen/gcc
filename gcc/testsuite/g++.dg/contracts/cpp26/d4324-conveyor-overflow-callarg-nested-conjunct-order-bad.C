// D4324/P2680 item 8's overflow scan: the mirror-image conjunct order
// inside a call argument -- 'x++' has nothing preceding it *within that
// same argument* to refine from, so it correctly still stays rejected,
// confirming the new recursion into a CALL_EXPR's own arguments doesn't
// over-generalize.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

void take (bool) conveyor {}

void f (int x) conveyor
{
  take (x++ < 2048 && x < 100000); // { dg-error "increment of .x. not provably free of overflow" }
}

int main () { f (1); return 0; }
