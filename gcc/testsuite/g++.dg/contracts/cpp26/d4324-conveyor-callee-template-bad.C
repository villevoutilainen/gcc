// D4324: the negative counterpart of d4324-conveyor-callee-template-
// ok.C -- a template callee that is NOT declared 'conveyor' is still
// correctly rejected once instantiated, for both a direct instantiation
// and a dependent call inside another template. See .claude/plans/
// well-we-last-discussed-ethereal-duckling.md.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

template <typename T>
T helper (T x) { return x + 1; }

int f (int x) conveyor
{
  helper<int> (x); // { dg-error "not declared .conveyor." }
  return 0;
}

template <typename T>
T g (T x) conveyor
{
  helper (x); // { dg-error "not declared .conveyor." }
  return T ();
}

int main () { return f (1) + g<int> (1); }
