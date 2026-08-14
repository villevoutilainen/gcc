// D4324/P2680 item 8's overflow scan: the same left-to-right conjunct
// refinement, for a bare '&&' used as a plain assignment's RHS (not an
// initializer).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

bool f (int x) conveyor
{
  bool ok = true;
  ok = x < 100000 && x++ < 2048;
  return ok;
}

int main () { return f (1) ? 0 : 1; }
