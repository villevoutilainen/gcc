// D4324, QoI: P3595R0's "function" and "module" match keys, at the top
// level and in a "caller" object.  "function" is a fully qualified name
// without template arguments, matching all overloads (and a member by
// its class); "module" matches the module a function is attached to, so
// in a translation unit with no modules it matches nothing.  Calls from
// a caller the "caller" object's "function" names get its semantic.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=ignore" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{"module":"m"},"output":{"semantic":"enforce"}},{"match":{"caller":{"module":"m"}},"output":{"semantic":"enforce"}},{"match":{"caller":{"function":"ours::caller"},"function":"vendor::f"},"output":{"semantic":"observe"}},{"match":{"function":"vendor::C::m"},"output":{"semantic":"observe"}},{"match":{"function":"vendor::g"},"output":{"semantic":"observe"}}\]} }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
using namespace std::contracts;

int calls;

void
handle_contract_violation (const contract_violation& v)
{
  ++calls;
  if (v.semantic () == evaluation_semantic::enforce)
    __builtin_abort ();	// no "module" entry may match
}

namespace vendor
{
  int f (int x) pre (x > 0) { return x; }
  int g (int x) pre (x > 0) { return x; }
  int g (double x) pre (x > 0) { return x; }
  struct C { int m (int x) pre (x > 0) { return x; } };
  int other (int x) pre (x > 0) { return x; }
}

namespace ours
{
  void caller () { vendor::f (0); }
  void not_caller () { vendor::f (0); }
}

int
main ()
{
  // Caller side: only from ours::caller.
  ours::caller ();
  if (calls != 1)
    __builtin_abort ();
  ours::not_caller ();
  if (calls != 1)
    __builtin_abort ();
  // "function": both overloads of vendor::g, and a member.
  vendor::g (0);
  vendor::g (0.0);
  vendor::C ().m (0);
  if (calls != 4)
    __builtin_abort ();
  // No entry for it: the translation unit's semantic, ignore.
  vendor::other (0);
  if (calls != 4)
    __builtin_abort ();
}
