// gimple_object_address_plugin.cc: under -fcontract-analysis-proofs=plugin
// a contract whose control object forces analysis is the plugin's to check
// too, without the proof flags, and the GIMPLE engine doesn't check it.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple -fcontract-analysis-proofs=plugin" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
namespace sc = std::contracts;

int divide (int a, int b) pre<sc::analyzed_symbolic_v> (b != 0);
int any (int x);

int t_literal () { return divide (10, 0); }		// { dg-error "argument .0. provably violates the precondition" }
int t_any (int x) { return divide (10, any (x)); }	// { dg-warning "cannot verify that .* is nonzero" }
int t_trusted (int x) pre<sc::analyzed_symbolic_v> (x != 0)
{
  return divide (10, x);
}
