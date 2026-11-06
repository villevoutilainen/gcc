// D4324: naming a control object on a contract inside a constexpr function
// actually invokes that object's protocol during constant evaluation
// (is_ignored/operator()), instead of falling back to the old P2900
// built-in evaluation-semantic path the way it used to.  A satisfied
// predicate compiles cleanly, proving the dispatch works end to end for
// the success path.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce" }

#include <contracts>

namespace sc = std::contracts;

constexpr int f (int x) pre<sc::mandatory_v>(x >= 0) { return x; }

static_assert (f (1) == 1);
static_assert (f (0) == 0);

constexpr void
g (int x)
{
  contract_assert<sc::mandatory_v>(x >= 0 || x < 0);
}

static_assert ((g (-5), true));
