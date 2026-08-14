// D4324/P2680 item 8's overflow scan: a '&&' nested one level *inside*
// another expression shape -- here, a call argument used as an 'if'
// condition -- is now reached, via oa_process_condition's own per-
// conjunct loop calling oa_scan_item8_conjunct (which recurses into a
// CALL_EXPR's own arguments) instead of flat-scanning the whole
// condition. Confirms the fix applies uniformly through
// oa_process_condition too, not just oa_scan_item8_in_expr's own new
// call sites -- this exact shape was rejected before either fix existed.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

bool take (bool b) conveyor { return b; }

void f (int x) conveyor
{
  if (take (x < 100000 && x++ < 2048)) {}
}

int main () { f (1); return 0; }
