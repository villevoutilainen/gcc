// D4324, QoI: a P3595 contract configuration gives each contract assertion
// the semantic of the first entry that matches it -- by kind, namespace and
// group (a control object's group() query, matched hierarchically) -- and
// -fcontract-evaluation-semantic the rest.  The control object is given
// that semantic: default_v (and a plain pre, which names it) and the
// labels follow it.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce" }
// { dg-additional-options {-fcontract-configuration=\[{"match":{"group":"lib"},"output":{"semantic":"observe"}},{"match":{"namespace":"quiet"},"output":{"semantic":"ignore"}},{"match":{"kind":"contract_assert"},"output":{"semantic":"observe"}}\]} }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <contract_labels>
using namespace std::contracts::P3400;

int calls;
std::contracts::evaluation_semantic last;

void
handle_contract_violation (const std::contracts::contract_violation& v)
{
  ++calls;
  last = v.semantic ();
  if (v.semantic () == std::contracts::evaluation_semantic::enforce)
    throw 0;
}

namespace quiet { namespace inner {
  int f (int x) pre (x > 0) { return x; }	// namespace quiet: ignored
} }
namespace loud {
  int g (int x) pre (x > 0) { return x; }	// no entry: enforced
  int h (int x) { contract_assert (x > 0); return x; } // kind: observed
  int lib (int x) pre<"lib.io"group>(x > 0) { return x; } // group lib: observed
  int library (int x) pre<"library"group>(x > 0) { return x; } // not lib: enforced
}

int
main ()
{
  using std::contracts::evaluation_semantic;
  if (quiet::inner::f (-1) != -1 || calls != 0)
    __builtin_abort ();
  try { loud::g (-1); __builtin_abort (); } catch (int) {}
  if (calls != 1 || last != evaluation_semantic::enforce)
    __builtin_abort ();
  if (loud::h (-1) != -1 || calls != 2 || last != evaluation_semantic::observe)
    __builtin_abort ();
  if (loud::lib (-1) != -1 || calls != 3 || last != evaluation_semantic::observe)
    __builtin_abort ();
  try { loud::library (-1); __builtin_abort (); } catch (int) {}
  if (calls != 4 || last != evaluation_semantic::enforce)
    __builtin_abort ();
}
