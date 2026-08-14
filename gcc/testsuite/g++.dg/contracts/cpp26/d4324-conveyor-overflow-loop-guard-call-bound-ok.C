// D4324/P2680 item 8's overflow scan: the case the type-bound witness
// design exists for specifically (see the plan's own "First design
// attempt, and why it was insufficient" section) -- 'i < v.size ()'
// establishes the witness from a CALL_EXPR bound, not a decl, proving
// the loop's own '++i' safe with no numeric fact about size ()'s own
// value ever needed. A design built only on widening which *kinds of
// decls* can serve as a bound (rather than accepting any expression
// shape at all, keyed purely on type) would never reach this case, since
// v.size () is not a decl in the first place.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct S { int size () const conveyor { return 3; } };

int use_while_loop_call_bound (S& v, int i) conveyor
{
  while (i < v.size ())
    ++i;
  return i;
}

int main ()
{
  S v;
  return use_while_loop_call_bound (v, 0) - 3;
}
