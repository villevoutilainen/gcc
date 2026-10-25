// D4324: the AST engine's loop analysis is linear-ish in loop nesting.
// Each loop walked its body several times (the head's fixed point, the
// checking pass, and one re-walk per reassigned variable), and a loop
// inside another was walked that many times again on every walk of the
// outer one: a nest 7 deep took hours.  The re-walks only matter for a
// loop nothing leaves, and an inner loop's fixed point now starts, in
// an outer loop's speculative walks, from its last head joined with the
// entry.  The facts each loop head keeps still reach the innermost
// body.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int deep (int n) conveyor
{
  int s = 0;
  for (int i0 = 1; i0 < n && i0 < 100; ++i0)
    for (int i1 = 1; i1 < n && i1 < 100; ++i1)
      for (int i2 = 1; i2 < n && i2 < 100; ++i2)
        for (int i3 = 1; i3 < n && i3 < 100; ++i3)
          for (int i4 = 1; i4 < n && i4 < 100; ++i4)
            for (int i5 = 1; i5 < n && i5 < 100; ++i5)
              for (int i6 = 1; i6 < n && i6 < 100; ++i6)
                s = 100 / i0 + 100 / i6;
  return s;
}
