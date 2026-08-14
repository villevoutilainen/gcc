// D4324/P2680 item 8's overflow scan: the same left-to-right conjunct
// refinement, for a bare '&&' used as a switch's own discriminant --
// arrives as 'CLEANUP_POINT_EXPR (CONVERT_EXPR (TRUTH_ANDIF_EXPR (...)))'
// (the extra CONVERT_EXPR converts to the switch's own integral selector
// type), so oa_collect_conjuncts needed to see through CONVERT_EXPR too,
// not just CLEANUP_POINT_EXPR.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int x) conveyor
{
  switch (x < 100000 && x++ < 2048)
    {
    case 0: return 0;
    default: return 1;
    }
}

int main () { return f (1) - 1; }
