// D4324, QoI: P3595R0's "dynamic" output.  A matching assertion's control
// object is std::contracts::dynamic_v<&F, ...>, which calls F, the
// configuration's selection function, once per evaluation for its
// semantic; this translation unit defines none, so the compiler's weak
// default returns the entry's "semantic", observe.  A violation reports
// the semantic F returned.  Calls from a caller a "caller" entry with a
// "dynamic" matches get a caller-side check with that control object.
// { dg-do run { target c++26 } }
// { dg-additional-options "-save-temps -fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=ignore" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{"caller":{"function":"ours::caller"},"function":"other::g"},"output":{"semantic":"observe","dynamic":{"name":"mylib::contract_semantic"}}},{"match":{"namespace":"vendor"},"output":{"semantic":"observe","dynamic":{"name":"::mylib::contract_semantic"}}}\]} }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
namespace sc = std::contracts;

int calls;

void
handle_contract_violation (const sc::contract_violation& v)
{
  if (v.semantic () != sc::evaluation_semantic::observe)
    __builtin_abort ();
  ++calls;
}

namespace vendor
{
  int f (int x) pre (x > 0) { return x; }
  int h (int x) { contract_assert (x > 0); return x; }
}

namespace other
{
  int g (int x) pre (x > 0) { return x; }
}

namespace ours
{
  void caller () { other::g (0); }
  void not_caller () { other::g (0); }
}

int
main ()
{
  vendor::f (0);
  vendor::h (0);
  vendor::f (1);
  if (calls != 2)
    __builtin_abort ();
  ours::caller ();
  if (calls != 3)
    __builtin_abort ();
  ours::not_caller ();	// ignored: the translation unit's semantic
  if (calls != 3)
    __builtin_abort ();
}

// { dg-final { scan-assembler {\.weak\s+_ZN5mylib17contract_semanticEv} } }
