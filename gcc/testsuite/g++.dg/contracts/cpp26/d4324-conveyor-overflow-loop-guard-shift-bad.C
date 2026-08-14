// D4324/P2680 item 8's overflow scan: the type-bound witness rescue
// only ever covers a shift of exactly 1 ('i + 1'/'i - 1', see
// oa_provably_safe_unit_shift_p's own header comment on why a bare
// 'i < e' fact can't prove anything stronger) -- the same loop-guard
// shape that rescues plain '++i' still, correctly, rejects 'i + 5'
// (k = 5 > 1), confirming that disclosed limit is actually enforced,
// not silently over-generalized to any literal shift.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int use_loop_guard_shift_bad (int i, int n) conveyor
{
  int j = 0;
  while (i < n)
    {
      j = i + 5; // { dg-error "not provably free of overflow in a conveyor function" }
      ++i;
    }
  return j;
}

int main () { return use_loop_guard_shift_bad (0, 3); }
