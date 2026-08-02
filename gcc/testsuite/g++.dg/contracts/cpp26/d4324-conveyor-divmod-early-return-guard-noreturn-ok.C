// D4324/P2680 item 8, Increment H: a guard whose terminating arm ends
// in a call to a function GCC already knows is noreturn
// (__builtin_trap, via call_expr_flags & ECF_NORETURN) is recognized
// as never falling through, the same as an explicit return -- so the
// surviving (else, implicit fall-through) branch's refined range is
// still used for the division.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int n) conveyor
{
  if (n < 1)
    __builtin_trap ();
  return 10 / n;
}

int main () { return f (5) - 2; }
