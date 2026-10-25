// D4324: a deep loop nest still checks its innermost body with each
// loop head's facts, and no more: I6 - 1 can be zero.
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
                s = 100 / i0 + 100 / (i6 - 1);	// { dg-error "not provably" }
  return s;
}
