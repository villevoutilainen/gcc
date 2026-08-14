// D4324/P2680 item 8's overflow scan: confirms oa_scan_item8_conjunct's
// new COND_EXPR handling actually scans *both branches* (not just the
// condition) -- 'a > 0' establishes nothing about 'b', so the division
// inside the true branch is still correctly rejected as unprovable.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int a, int b) conveyor
{
  return (a > 0) ? 10 / b : 0; // { dg-error "divisor .b. not provably nonzero" }
}

int main () { return f (1, 2); }
