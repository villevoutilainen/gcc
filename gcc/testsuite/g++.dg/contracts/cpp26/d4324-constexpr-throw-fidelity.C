// D4324: assertion_context::check() stays genuinely lazy under constant
// evaluation, called from inside the control object's own operator() --
// not precomputed by the compiler beforehand.  C++26 permits throwing and
// catching during constant evaluation; a control object whose operator()
// wraps ctx.check() in its own try/catch can therefore genuinely intercept
// an exception thrown while evaluating the predicate, even at compile time,
// exactly mirroring runtime behavior ("D4324 leaves it to propagate to the
// nearest noexcept boundary" -- here, that boundary is this operator()'s
// own catch).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce" }

#include <contracts>

namespace sc = std::contracts;

struct throwing_predicate_control {
  static constexpr bool is_ignored (sc::evaluation_semantic) { return false; }
  static constexpr bool constify (sc::evaluation_semantic) { return false; }
  static constexpr bool assumable (sc::evaluation_semantic) { return false; }

  constexpr void
  operator() (const sc::assertion_context& ctx) const
  {
    try
      {
	if (ctx.check ())
	  return;
      }
    catch (int caught)
      {
	// Proves the exception genuinely propagated out of ctx.check() and
	// was caught HERE, inside operator()'s own dynamic scope -- not
	// pre-evaluated by the compiler before operator() even ran.
	if (caught == 42)
	  return; // treat as "handled", proceed
	throw;
      }
  }
};

inline constexpr throwing_predicate_control throwing_v{};

constexpr bool
throws_42_if_negative (int x)
{
  if (x < 0)
    throw 42;
  return true;
}

constexpr int f (int x) pre<throwing_v>(throws_42_if_negative (x)) { return x; }

// The predicate throws 42; throwing_v's own try/catch intercepts it and
// treats it as "handled", so the call succeeds despite the throw.
static_assert (f (-1) == -1);
static_assert (f (1) == 1);
