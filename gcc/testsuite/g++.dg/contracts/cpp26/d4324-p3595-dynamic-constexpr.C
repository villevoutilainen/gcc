// D4324, QoI: P3595R0's "dynamic" output: a constant evaluation can't call
// the selection function, so it uses the entry's "semantic", here ignore,
// whatever the function returns at run time.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{},"output":{"semantic":"ignore","dynamic":{"name":"mylib::contract_semantic"}}}\]} }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

constexpr int f (int x) pre (x > 0) { return x; }
static_assert (f (-1) == -1);
