// D4324: a local variable of class type, initialized via a constructor
// call (not a plain scalar initializer), must not be misdiagnosed as
// "not explicitly initialized" -- DECL_INITIAL does not reliably survive
// to finish_function time for such variables, so the check has to
// consult the as-parsed initializer at cp_finish_decl time instead.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct S { int v; ~S () conveyor {} };

bool f (int x) conveyor
{
  S s{x};
  return s.v > 0;
}

int main () { return f (1) ? 0 : 1; }
