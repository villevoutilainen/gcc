// D4324/P2680 item 8's overflow scan: the type-bound witness route,
// same idea as d4324-conveyor-overflow-loop-guard-for-ok.C but spelled
// as a while loop with '++i' in the body instead of a for-loop's own
// increment-clause -- confirms the fix is genuinely general (oa_refine_
// single_comparison's new matcher fires from any control-flow condition
// refinement, not specifically a for-loop's own shape).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int use_while_loop_guard (int i, int n) conveyor
{
  while (i < n)
    ++i;
  return i;
}

int main () { return use_while_loop_guard (0, 3) - 3; }
