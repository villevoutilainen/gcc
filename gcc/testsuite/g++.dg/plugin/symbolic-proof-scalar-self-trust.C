// symbolic_proof_plugin.cc: a function's own symbolic precondition's
// range is trusted for its parameter in its body, and a range the
// caller's code established (conveyor's facts) satisfies a symbolic
// precondition too, as for the built-in -fcontract-symbolic-proofs.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
using namespace std::contracts;

void consumer (int x) pre<symbolic_assert_v>(x >= 20 && x < 100) { (void) x; }

void self_trust (int x) pre<symbolic_assert_v>(x >= 20 && x < 100) { consumer (x); }
void refined (int x) { if (x >= 30 && x < 40) consumer (x); }
void unknown (int x) { consumer (x); }	// { dg-warning "cannot verify that .x. satisfies" }

int main () { return 0; }
