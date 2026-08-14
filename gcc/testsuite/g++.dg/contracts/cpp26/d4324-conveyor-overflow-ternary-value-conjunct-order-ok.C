// D4324/P2680 item 8's overflow scan: a '&&' nested one level inside a
// ternary's own condition, with the ternary used as a *value* (not a
// bare statement) -- oa_scan_item8_conjunct recognizes the COND_EXPR
// shape and reuses oa_process_condition's own then/else split on the
// condition, exactly as if this had been written as an ordinary 'if'.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int x) conveyor
{
  int y = (x < 100000 && x++ < 2048) ? 1 : 2;
  return y;
}

int main () { return f (1) - 1; }
