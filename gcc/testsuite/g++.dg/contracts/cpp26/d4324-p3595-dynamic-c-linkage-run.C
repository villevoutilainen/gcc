// D4324, QoI: P3595R0's "dynamic" output with "linkage": "C".  A name
// that is an identifier is an extern "C" function, here declared before
// its use (noexcept, which is fine) and defined after; one that isn't is
// a symbol, here a C++ function's mangled name, with "provideweak": false
// so this translation unit's definition of that function is the one.
// { dg-do run { target c++26 } }
// { dg-additional-options "-save-temps -fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=ignore" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{"namespace":"a"},"output":{"semantic":"enforce","dynamic":{"name":"a_semantic","linkage":"C"}}},{"match":{"namespace":"b"},"output":{"dynamic":{"name":"_ZN1b10b_semanticEv","linkage":"C","provideweak":false}}}\]} }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
namespace sc = std::contracts;

int a_calls, b_calls, handler_calls;

void
handle_contract_violation (const sc::contract_violation&)
{
  ++handler_calls;
}

extern "C" sc::evaluation_semantic a_semantic () noexcept;

namespace a { int f (int x) pre (x > 0) { return x; } }
namespace b { int f (int x) pre (x > 0) { return x; } }

extern "C" sc::evaluation_semantic
a_semantic () noexcept
{
  ++a_calls;
  return sc::evaluation_semantic::observe;
}

namespace b
{
  sc::evaluation_semantic
  b_semantic ()
  {
    ++b_calls;
    return sc::evaluation_semantic::observe;
  }
}

int
main ()
{
  a::f (0);
  b::f (0);
  if (a_calls != 1 || b_calls != 1 || handler_calls != 2)
    __builtin_abort ();
}

// { dg-final { scan-assembler-not {\.weak\s+(a_semantic|_ZN1b10b_semanticEv)} } }
