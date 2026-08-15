// D4324: the loud-on-broken-trait fix applies uniformly to the seven
// *optional* trait members too, not just is_ignored/constify/assumable
// -- a present-but-broken omit_comment (here, calling a non-constexpr
// function) must be diagnosed the same way, since silently defaulting
// to "include the comment anyway" for a control type that specifically
// asked to suppress it is just as real a correctness gap as a broken
// is_ignored.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

namespace sc = std::contracts;

bool not_constexpr (); // never defined; not constexpr-callable

struct broken_omit_comment {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool
  omit_comment (sc::assertion_static_info)
  { return not_constexpr (); }

  void
  operator() (const sc::assertion_context& ctx) const
  {
    if (!ctx.check ())
      __builtin_abort ();
  }
};

inline constexpr broken_omit_comment broken_omit_comment_v{};

// { dg-error "does not produce a constant expression" "" { target *-*-* } 0 }
// { dg-error "called in a constant expression" "" { target *-*-* } 0 }
// { dg-error "call to non-.constexpr. function" "" { target *-*-* } 0 }
int f (int x) pre<broken_omit_comment_v>(x >= 0)
{ return x; }
