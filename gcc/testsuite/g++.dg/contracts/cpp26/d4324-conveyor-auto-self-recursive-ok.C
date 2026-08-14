// D4324: a 'conveyor(auto)' function template that calls itself,
// directly (same specialization) or, by the same mechanism,
// indirectly through another conveyor(auto) function, must not hang
// or crash the compiler. The recursive call is checked against the
// enclosing specialization's own, optimistically-assumed-conveyor
// status while its body is still being resolved (the same status a
// plain, eagerly-conveyor function's own recursive calls already see,
// since bare 'conveyor' is set before the body is even parsed) --
// deduction only concludes "not conveyor" if something else in the
// body actually violates a mandatory rule, not merely because of the
// self-reference itself.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

template<typename _Tp>
int
countdown (_Tp x, unsigned depth) conveyor(auto)
{
  if (depth == 0)
    return x;
  return countdown (x, depth - 1);
}

int user (int x) conveyor
{ return countdown (x, 3); }

int
main ()
{
  return user (1) == 1 ? 0 : 1;
}
