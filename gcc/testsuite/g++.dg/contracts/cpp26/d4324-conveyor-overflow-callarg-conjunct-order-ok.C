// D4324/P2680 item 8's overflow scan: a bare call statement's own '&&'
// argument is now reached by the left-to-right refinement -- the '&&' is
// nested one level inside the CALL_EXPR (oa_collect_conjuncts only ever
// looks at its own argument's *top* level), so oa_scan_item8_conjunct
// recurses into each of the call's own arguments independently (each
// argument's own evaluation is fully sequenced within itself, even
// though the relative order *between* arguments is unspecified) rather
// than leaving it to a flat, unrefined scan.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

void take (bool) conveyor {}

void f (int x) conveyor
{
  take (x < 100000 && x++ < 2048);
}

int main () { f (1); return 0; }
