// D4324, Increment O: closes the CLEANUP_STMT gap found via the
// pre-existing d4324-conveyor-raii-ok.C regressing when oa_stmt_
// terminates_p first learned to answer this question for real -- a
// local variable with a non-trivial destructor wraps the rest of its
// scope in a CLEANUP_STMT (a try/finally shape), which oa_stmt_
// terminates_p didn't recognize at all, conservatively concluding
// "might fall through" even when every exit path genuinely returns.
// Both branches returning, with a non-trivial destructor in scope,
// must be accepted.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct S { int v; ~S () conveyor {} };

int f (bool flag) conveyor
{
  S s{1};
  if (flag)
    return s.v;
  else
    return -s.v;
}

int main () { return f (true) - 1; }
