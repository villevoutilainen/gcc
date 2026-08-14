// D4324/P2680 item 8's overflow scan: the same left-to-right conjunct
// refinement, for a bare '&&' used as its own, discarded expression-
// statement (the generic default-scan fallback, oa_walk_stmt's own
// 'default:' case) -- unlike a call argument, the '&&' genuinely is this
// statement's own top-level expression here, so it is decomposed into
// conjuncts and refined correctly.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

void f (int x) conveyor
{
  x < 100000 && x++ < 2048;
}

int main () { f (1); return 0; }
