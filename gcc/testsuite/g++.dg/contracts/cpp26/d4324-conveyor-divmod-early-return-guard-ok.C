// D4324/P2680 item 8, Increment H: the common early-return-guard idiom
// -- 'if (n <= 0) return 0;' with no else -- now correctly narrows n's
// range for the code after the if. Previously the if/else merge
// unconditionally unioned both branches' facts regardless of whether
// the then-branch always returns; since ELSE_CLAUSE is absent here
// (falls through trivially) and THEN_CLAUSE always returns, only the
// else-branch's refined range (n > 0) should survive to the division.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int n) conveyor
{
  if (n <= 0)
    return 0;
  return 10 / n;
}

int main () { return f (5) - 2; }
