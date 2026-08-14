// D4324: the callee-must-be-conveyor check runs on the concrete
// instantiation's own FUNCTION_DECL (DECL_DECLARED_CONVEYOR_P survives
// tsubst_function_decl's own copy_decl the same way DECL_DECLARED_
// CONSTEXPR_P does), for both a directly-instantiated template callee
// and a dependent call inside another template. See .claude/plans/
// well-we-last-discussed-ethereal-duckling.md.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

template <typename T>
T helper (T x) conveyor { return x; }

int f (int x) conveyor
{
  return helper<int> (x); // direct instantiation
}

template <typename T>
T g (T x) conveyor
{
  return helper (x); // dependent call, resolved at instantiation time
}

int main () { return f (1) + g<int> (1) - 2; }
