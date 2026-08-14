// D4324/P2680 item 8, Increment N: closes the "a loop's own condition
// gets no item-7/item-8 scanning" gap -- a division reached as a
// *sub-expression* of the loop's own condition (not the condition's
// entire top-level code, the only shape oa_walk_stmt's own CALL_EXPR
// case would otherwise catch) is now scanned, the same treatment
// IF_STMT/COND_EXPR/SWITCH_STMT's own conditions already get.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int n, int i) conveyor
{
  if (n < 1)
    return 0;
  if (i < 0)
    return 0;
  int total = 0;
  for (; i < 10 && 10 / n > 0; i++)
    total += i;
  return total;
}

int main () { return f (5, 0) == 45 ? 0 : 1; }
