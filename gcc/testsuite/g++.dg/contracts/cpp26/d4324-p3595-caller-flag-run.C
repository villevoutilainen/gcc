// D4324, QoI: P3595R0's "caller" key with -fcontracts-client-check, for
// C++26 contracts without control objects.  The flag stands for a last
// {"match": {"caller": true, "kind": "pre"}} entry with the translation
// unit's semantic, so an earlier "caller" entry decides first: here it
// turns the caller-side checks of our own functions off, and the
// vendor's preconditions are checked on both sides.  Entries without
// "caller" don't apply to caller-side checks.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-evaluation-semantic=observe -fcontracts-client-check=pre" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{"caller":true,"namespace":"ours"},"output":{"semantic":"ignore"}},{"match":{"namespace":"vendor"},"output":{"semantic":"ignore"}}\]} }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

int calls;

void
handle_contract_violation (const std::contracts::contract_violation&)
{ ++calls; }

namespace vendor { int f (int x) pre (x > 0) { return x; } }
namespace ours { int g (int x) pre (x > 0) { return x; } }

int
main ()
{
  // Checked in the definition only (observe); the caller-side check is
  // ignored by the first entry.
  ours::g (0);
  if (calls != 1)
    __builtin_abort ();
  // Checked on the caller's side by the flag (observe); the vendor's
  // definition is ignored by the second entry, which doesn't apply to the
  // caller-side check.
  vendor::f (0);
  if (calls != 2)
    __builtin_abort ();
}
