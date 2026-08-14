// D4324/P2680 item 8's overflow scan: the type-bound witness route
// established from a plain 'if' guard (no loop at all) protecting a
// bare '++i;' -- confirms the fix isn't loop-specific; oa_refine_single_
// comparison's new matcher fires from any control-flow condition
// refinement, if or loop alike.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int use_if_guarded_increment (int i, int n) conveyor
{
  if (i < n)
    ++i;
  return i;
}

int main () { return use_if_guarded_increment (2, 5) - 3; }
