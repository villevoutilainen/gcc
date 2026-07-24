// D4324: at compile time, ignore_v and assume_v are both skipped entirely --
// the control object is never touched and the predicate is never evaluated
// -- exactly mirroring their runtime "zero cost" behavior.  This holds even
// for assume_v, which is also assumable: unlike at runtime (where the
// predicate is handed to the optimizer as an assumption via IFN_ASSUME),
// there is no optimizer-assumption concept during constant evaluation, so
// ignored-and-assumable collapses to the same "skip, don't evaluate at all"
// outcome as ignored-and-not-assumable, rather than being evaluated and
// enforced.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce" }

#include <contracts>

namespace sc = std::contracts;

// Never defined: if this were ever called, linking (or, here, merely
// requiring a definition under -pedantic-errors) would fail -- so a clean
// compile is itself proof the predicate was never evaluated.
bool never_called (int x);

constexpr int f (int x) pre<sc::ignore_v>(never_called (x)) { return x; }
constexpr int g (int x) pre<sc::assume_v>(never_called (x)) { return x; }

static_assert (f (1) == 1);
static_assert (f (-1) == -1);
static_assert (g (1) == 1);
static_assert (g (-1) == -1);
