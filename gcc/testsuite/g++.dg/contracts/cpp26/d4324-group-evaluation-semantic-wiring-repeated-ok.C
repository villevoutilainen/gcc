// D4324: -fcontracts-group-evaluation-semantic= is repeatable, and every
// occurrence -- including more than one for the *same* group name --
// is preserved in command-line order in the raw table: this layer does
// no deduplication or "first/last wins" resolution itself (that's a
// consumer-level policy, e.g. P3400's group-based semantic override,
// not this data-plumbing layer's job).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontracts-group-evaluation-semantic=safety:observe -fcontracts-group-evaluation-semantic=perf:ignore -fcontracts-group-evaluation-semantic=safety:enforce" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
namespace sc = std::contracts;

int seen_count = -1;
const char* names[8] {};
sc::evaluation_semantic semantics[8] {};

struct probe_ctrl {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  {
    auto rules = ctx.static_info ().group_semantic_rules ();
    seen_count = (int) rules.size ();
    int i = 0;
    for (auto& rule : rules)
      {
	names[i] = rule.group_name ();
	semantics[i] = rule.semantic ();
	++i;
      }
    if (!ctx.check ())
      __builtin_abort ();
  }
};
inline constexpr probe_ctrl probe_v{};

int f (int x) pre<probe_v>(x > 0) { return x; }

int
main ()
{
  if (f (5) != 5)
    __builtin_abort ();
  if (seen_count != 3)
    __builtin_abort ();
  if (__builtin_strcmp (names[0], "safety") != 0
      || semantics[0] != sc::evaluation_semantic::observe)
    __builtin_abort ();
  if (__builtin_strcmp (names[1], "perf") != 0
      || semantics[1] != sc::evaluation_semantic::ignore)
    __builtin_abort ();
  if (__builtin_strcmp (names[2], "safety") != 0
      || semantics[2] != sc::evaluation_semantic::enforce)
    __builtin_abort ();
  return 0;
}
