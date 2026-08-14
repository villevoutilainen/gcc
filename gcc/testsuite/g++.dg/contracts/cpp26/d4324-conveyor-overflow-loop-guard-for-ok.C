// D4324/P2680 item 8's overflow scan: the type-bound witness route (see
// oa_type_bound_fact's own comment) -- 'i < n' establishes a witness
// for i, sufficient (via the type invariant 'n <= TYPE_MAX' alone, with
// no numeric fact about n's own value ever needed) to prove '++i' safe
// inside the loop, for a fully unconstrained parameter n. This is the
// motivating regression-avoidance case found while designing this scan
// (see the plan's own "Critical design constraint" section): a naive,
// numeric-only overflow check would have wrongly flagged this extremely
// common idiom.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int use_for_loop_guard (int n) conveyor
{
  int b = 3;
  for (int i = 0; i < n; ++i)
    b = 5;
  return 10 / b;
}

int main () { return use_for_loop_guard (2) - 2; }
