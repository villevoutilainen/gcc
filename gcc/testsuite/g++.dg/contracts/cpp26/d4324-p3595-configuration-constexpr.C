// D4324, QoI: a P3595 entry's "constexpr" key selects the assertion's
// evaluation at compile time (true) or at run time (false): here it is
// ignored at compile time, so the constant evaluation below succeeds, and
// enforced at run time.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{"constexpr":true},"output":{"semantic":"ignore"}}\]} }

#include <contracts>

constexpr int f (int x) pre (x > 0) { return x; }
constexpr int a = f (-1);	// not checked at compile time
