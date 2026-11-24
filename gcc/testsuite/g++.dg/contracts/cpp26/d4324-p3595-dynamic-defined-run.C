// D4324, QoI: P3595R0's "dynamic" output, with the selection function
// defined in this translation unit (after its use), so no weak default is
// emitted: each evaluation calls it once, and its semantic decides --
// ignore skips the check, observe reports the violation and continues.
// Without a "semantic", a constant evaluation uses dynamic_control's
// default, enforce.
// { dg-do run { target c++26 } }
// { dg-additional-options "-save-temps -fcontracts -fcontract-control-objects" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{"kind":"pre"},"output":{"dynamic":{"name":"mylib::contract_semantic"}}}\]} }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
namespace sc = std::contracts;

int handler_calls, selections;
sc::evaluation_semantic next = sc::evaluation_semantic::ignore;

void
handle_contract_violation (const sc::contract_violation& v)
{
  if (v.semantic () != sc::evaluation_semantic::observe)
    __builtin_abort ();
  ++handler_calls;
}

int f (int x) pre (x > 0) { return x; }

constexpr int g (int x) pre (x > 0) { return x; }
static_assert (g (1) == 1);

namespace mylib
{
  sc::evaluation_semantic
  contract_semantic ()
  {
    ++selections;
    return next;
  }
}

int
main ()
{
  f (0);
  if (selections != 1 || handler_calls != 0)
    __builtin_abort ();
  next = sc::evaluation_semantic::observe;
  f (0);
  f (1);
  if (selections != 3 || handler_calls != 1)
    __builtin_abort ();
}

// { dg-final { scan-assembler-not {\.weak\s+_ZN5mylib17contract_semanticEv} } }
