// D4324/P3400: the labels of <contract_labels> match configured group
// names hierarchically: a rule for "lib" covers the label groups "lib" and
// "lib.io", not "library".  std::contracts::P3400::group_matches is the
// same test, for a control object of the user's own.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce -fcontracts-group-evaluation-semantic=lib:ignore" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contract_labels>
namespace P3400 = std::contracts::P3400;
using namespace std::contracts::P3400;

static_assert (group_matches ("lib", "lib"));
static_assert (group_matches ("lib", "lib.io"));
static_assert (group_matches ("lib.io", "lib.io.async"));
static_assert (!group_matches ("lib", "library"));
static_assert (!group_matches ("lib.io", "lib"));
static_assert (!group_matches ("lib", ""));

int calls;

void
handle_contract_violation (const std::contracts::contract_violation&)
{
  ++calls;
  throw 0;
}

int nested (int x) pre<"lib.io"group>(x > 0) { return x; }
int exact (int x) pre<"lib"group>(x > 0) { return x; }
int other (int x) pre<"library"group>(x > 0) { return x; }

int
main ()
{
  // lib:ignore covers lib and lib.io: not checked.
  if (nested (-1) != -1 || exact (-1) != -1)
    __builtin_abort ();
  // ...but not library: enforced, the handler throws.
  try { other (-1); __builtin_abort (); } catch (int) {}
  if (calls != 1)
    __builtin_abort ();
}
