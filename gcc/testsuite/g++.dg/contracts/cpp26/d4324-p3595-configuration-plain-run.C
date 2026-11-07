// D4324, QoI: a P3595 contract configuration also gives C++26 contracts
// without control objects their semantic, matched by kind and namespace.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-evaluation-semantic=enforce" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{"namespace":"obs"},"output":{"semantic":"observe"}},{"match":{"kind":"post"},"output":{"semantic":"ignore"}}\]} }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

int calls;
std::contracts::evaluation_semantic last;

void
handle_contract_violation (const std::contracts::contract_violation& v)
{
  ++calls;
  last = v.semantic ();
  if (v.semantic () == std::contracts::evaluation_semantic::enforce)
    throw 0;
}

namespace obs { int f (int x) pre (x > 0) { return x; } }
int g (int x) pre (x > 0) { return x; }
int h (int x) post (r : r > 0) { return x; }

int
main ()
{
  if (obs::f (-1) != -1 || calls != 1
      || last != std::contracts::evaluation_semantic::observe)
    __builtin_abort ();
  try { g (-1); __builtin_abort (); } catch (int) {}
  if (calls != 2)
    __builtin_abort ();
  if (h (-1) != -1 || calls != 2)
    __builtin_abort ();
}
