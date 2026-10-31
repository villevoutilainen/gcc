// [expr.prim.lambda.closure]/p10 with a captured reference: the
// diagnostic's "declared here" note used to ICE on the reference's
// implicit dereference.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts" }

template<typename T>
int
f (T& r, int v)
{
  auto g = [&] (int b) {
    contract_assert (&r != nullptr); // { dg-error {'r' is not implicitly captured by a contract assertion} }
    return b ? v : b;
  };
  return g (1);
}

int x;
int y = f (x, 2);
