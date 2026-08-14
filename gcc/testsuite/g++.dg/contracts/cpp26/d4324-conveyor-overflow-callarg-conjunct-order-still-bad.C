// D4324/P2680 item 8's overflow scan: a bare call statement's own '&&'
// argument is *not* reached by the new left-to-right refinement -- the
// '&&' is nested one level inside the CALL_EXPR (oa_collect_conjuncts
// only ever looks at its own argument's *top* level), so this stays
// conservatively rejected. A disclosed, pre-existing limitation (see
// oa_scan_item8_in_expr's own comment), not something this fix covers;
// confirmed the exact same shape was already rejected as an 'if'
// condition before any of this session's changes (d4324-conveyor-
// overflow-nested-conjunct-still-bad.C).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

void take (bool) conveyor {}

void f (int x) conveyor
{
  take (x < 100000 && x++ < 2048); // { dg-error "increment of .x. not provably free of overflow" }
}

int main () { f (1); return 0; }
