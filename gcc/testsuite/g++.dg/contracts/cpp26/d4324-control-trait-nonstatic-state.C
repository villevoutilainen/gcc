// D4324: is_ignored/constify/assumable (and the other control-object
// trait queries dispatched through the same shared mechanism) may now be
// non-static, in which case the compiler uses the real, constant-
// evaluated control object itself as *this -- not a dummy placeholder --
// so a trait can genuinely consult per-instance state, exactly as
// operator() already could (see d4324-control-object-state.C).  Checked
// both via named constexpr objects (distinct instances of the same
// type, carrying different thresholds) and via a prvalue temporary
// named directly in pre<...>, so this isn't somehow tied to one form or
// the other.  A real, deliberately-triggered violation on the
// not-ignored instance (the low-threshold one) proves is_ignored's own
// per-instance read genuinely drove the dispatch decision, not just
// that the predicate itself happened to hold.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

struct thresholded {
  int threshold;
  constexpr bool
  is_ignored (sc::assertion_static_info) const	// non-static
  { return threshold > 100; }
  constexpr bool
  constify (sc::assertion_static_info) const { return true; }
  constexpr bool
  assumable (sc::assertion_static_info) const { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  {
    // Reached at all only when is_ignored's own (non-static, real
    // per-instance) read said "not ignored".  If the predicate itself
    // holds, there's nothing more to do.
    if (ctx.check ())
      return;
    // A genuinely checked-and-violated call: end the program here,
    // successfully, so main's own tail is unreachable in the expected
    // run.
    __builtin_exit (0);
  }
};

inline constexpr thresholded low{50};
inline constexpr thresholded high{200};

int f (int x) pre<low>(x > 0) { return x; }	// not ignored: checked
int g (int x) pre<high>(x > 0) { return x; }	// ignored: never checked

// A prvalue temporary control object, not a named variable.
int h (int x) pre<thresholded{200}>(x > 0) { return x; }

int
main ()
{
  if (f (1) != 1)
    __builtin_abort ();
  // high/thresholded{200}'s is_ignored reads threshold=200 > 100 -> true:
  // the assertion below would violate (x <= 0) but must never be checked
  // at all, so operator() must never run (and never reach __builtin_exit).
  if (g (-1) != -1)
    __builtin_abort ();
  if (h (-1) != -1)
    __builtin_abort ();
  // low's is_ignored reads threshold=50 > 100 -> false: genuinely
  // checked, and this call violates (x <= 0) -> operator() runs,
  // ctx.check() is false -> __builtin_exit (0) above.
  f (-1);
  __builtin_abort ();	// unreachable in the expected run
}
