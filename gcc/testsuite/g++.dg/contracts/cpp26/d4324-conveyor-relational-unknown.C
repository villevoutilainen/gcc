// D4324: the reported gap this feature closes -- g's own precondition
// only guarantees "x < 7" (its own bound), but calling f(x) relies on
// f's own default argument for its second parameter, a value entirely
// unrelated to g's own q (not forwarded, not a literal at this call
// site) -- so there is no established relational fact to consult at
// all, and this must report "cannot verify", not silently pass.
// Before this feature, this exact shape produced no diagnostic
// whatsoever, since neither literal-range checking (oa_match_simple_
// comparison, which needs a literal bound in the callee's own declared
// text) nor anything else recognized "compared against another
// parameter" at all. See .claude/plans/well-we-last-discussed-
// ethereal-duckling.md.
//
// Also now exercises the separate postcondition-verification feature
// (merged-across-every-return fact checking, see oa_handle_
// postcondition_stmt's own comment): g's own postcondition 'r < q'
// (g's own q, defaulted 7) is likewise genuinely unprovable from g's
// own body -- f(x)'s returned value is only known to be < f's own,
// unrelated default q (5), never established as a relation to g's own
// q at all -- so g's own postcondition declaration correctly gets its
// own, separate "cannot verify" too.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct conveyor_ctrl {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool is_conveyor (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr conveyor_ctrl ctrl_v{};

int f (int x, int const q = 5) pre<ctrl_v> (x < q) post<ctrl_v> (r: r < q) { return x; }
int g (int x, int const q = 7) pre<ctrl_v> (x < q)
     post<ctrl_v> (r: r < q) // { dg-warning "cannot verify postcondition condition" }
{
  return f (x); // { dg-warning "cannot verify that .x. satisfies the precondition" }
}

int main () { return g (2) - 2; }
