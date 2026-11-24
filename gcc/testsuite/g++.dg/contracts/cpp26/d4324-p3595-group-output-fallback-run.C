// D4324, QoI: the library's control objects take a group a P3595
// configuration assigns when the code gives them none, so that
// -fcontracts-group-evaluation-semantic= rules apply to it; a group the
// code gives wins.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=ignore -fcontracts-group-evaluation-semantic=vendor.io:observe" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{"namespace":"vendor"},"output":{"group":"vendor.io"}}\]} }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
namespace sc = std::contracts;
using namespace std::contracts::controls;

int handler_calls;

void
handle_contract_violation (const sc::contract_violation& v)
{
  if (v.semantic () != sc::evaluation_semantic::observe)
    __builtin_abort ();
  ++handler_calls;
}

namespace vendor
{
  int f (int x) pre (x > 0) { return x; }			// observe
  int g (int x) pre<annotated ("app"group)> (x > 0) { return x; }	// ignore
  void h (int x) { contract_assert<annotated ("message")> (x > 0); } // observe
}

int
main ()
{
  vendor::f (0);
  vendor::g (0);
  vendor::h (0);
  if (handler_calls != 2)
    __builtin_abort ();
}
