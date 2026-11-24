// D4324: a P3595 configuration spells the quick-enforce semantic
// "quick-enforce", as the paper does; -fcontract-evaluation-semantic's
// "quick_enforce" is accepted too.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{"kind":"pre"},"output":{"semantic":"quick-enforce"}},{"match":{"kind":"post"},"output":{"semantic":"quick_enforce"}}\]} }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

int f (int x) pre (x > 0) { return x; }
int g (int x) post (r : r > 0) { return x; }
