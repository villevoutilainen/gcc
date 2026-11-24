// D4324, QoI: P3595R0's "dynamic" output without a "semantic": the weak
// default of the selection function returns dynamic_control's default,
// enforce, chosen by the library, not the compiler.
// { dg-do run { target c++26 } }
// { dg-additional-options "-save-temps -fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=ignore" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{},"output":{"dynamic":{"name":"contract_semantic","linkage":"C"}}}\]} }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <cstdlib>
namespace sc = std::contracts;

void
handle_contract_violation (const sc::contract_violation& v)
{
  if (v.semantic () == sc::evaluation_semantic::enforce)
    std::exit (0);
  __builtin_abort ();
}

int f (int x) pre (x > 0) { return x; }

int
main ()
{
  f (0);
  __builtin_abort ();
}

// { dg-final { scan-assembler {\.weak\s+contract_semantic} } }
