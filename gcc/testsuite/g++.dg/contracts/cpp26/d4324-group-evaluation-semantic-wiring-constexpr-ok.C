// D4324: assertion_static_info::group_semantic_rules() must be usable
// at compile time, not just at runtime -- is_ignored (dispatched via
// constant evaluation only, see contract_control_bool_member) reads it
// directly to decide, for real, whether to skip an assertion entirely,
// proving the underlying compiler-synthesized table is genuinely
// usable in a constant expression (not just readable after the fact
// from within operator()'s ordinary runtime code, which is the easier
// case -- see the -wiring-basic-ok.C/-wiring-repeated-ok.C tests).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce -fcontracts-group-evaluation-semantic=opt:ignore" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
namespace sc = std::contracts;

// A group-name-checking control type with no P3400 involved at all:
// is_ignored itself scans assertion_static_info::group_semantic_rules()
// for GROUP, and is ignored only if a matching rule says "ignore".
template <int N>
struct group_probe_t {
  static constexpr const char* GROUP = N == 0 ? "opt" : "other";

  static constexpr bool
  is_ignored (sc::assertion_static_info info)
  {
    for (auto& rule : info.group_semantic_rules ())
      if (__builtin_strcmp (rule.group_name (), GROUP) == 0)
	return rule.semantic () == sc::evaluation_semantic::ignore;
    return false;
  }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  {
    // Called at all only when is_ignored said "not ignored" -- and,
    // per this fork's dispatch model, unconditionally even when the
    // predicate holds, so this must check ctx.check() itself.
    if (ctx.check ())
      return;
    __builtin_abort (); // never reached in this test: g(5) doesn't violate,
			// and f(-1) is ignored before operator() ever runs
  }
};

inline constexpr group_probe_t<0> opt_probe{};   // "opt": has a rule, ignore
inline constexpr group_probe_t<1> other_probe{}; // "other": no rule, stays enforced

int f (int x) pre<opt_probe>(x > 0) { return x; }
int g (int x) pre<other_probe>(x > 0) { return x; }

int
main ()
{
  // opt_probe's group is genuinely ignored (its own is_ignored, run at
  // compile time, found the "opt:ignore" rule): a violation must not
  // even be checked, let alone call operator()'s __builtin_abort().
  if (f (-1) != -1)
    __builtin_abort ();
  // other_probe's group has no matching rule, so it stays fully
  // checked/enforced under this TU's own -fcontract-evaluation-
  // semantic=enforce.
  if (g (5) != 5)
    __builtin_abort ();
  return 0;
}
