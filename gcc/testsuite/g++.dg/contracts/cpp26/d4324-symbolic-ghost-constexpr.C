// D4324 (P4425): a symbolic contract's predicate is never evaluated --
// only the ghost state of the facts the analysis tracks is -- also not
// during constant evaluation, which has none: a false or non-constant
// predicate changes nothing there.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

bool runtime_only ();	// not constexpr
struct W {
  int state = 0;
  constexpr bool big () const { return state >= 20; }
  constexpr int use () const pre<std::contracts::symbolic_assert_v> (big ())
  { return state; }
  constexpr int other () const
    pre<std::contracts::symbolic_assert_v> (runtime_only ())
  { return state; }
};
static_assert (W{25}.use () == 25);
static_assert (W{0}.use () == 0);	// false predicate: not evaluated
static_assert (W{3}.other () == 3);	// non-constant predicate: not evaluated
