// D4324: -fcontracts-group-evaluation-semantic=group:semantic now flows
// through as real data on assertion_static_info::group_semantic_rules()
// -- a genuine std::contracts member, usable by any control type, not
// just a P3400 label (P3400's own consumption of this is a separate
// change). This is a basic end-to-end check with a single rule: a
// plain, non-P3400 control type reads the rule back directly.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontracts-group-evaluation-semantic=safety:observe" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
namespace sc = std::contracts;

int seen_count = -1;
const char* seen_name = nullptr;
sc::evaluation_semantic seen_semantic {};

struct probe_ctrl {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  {
    auto rules = ctx.static_info ().group_semantic_rules ();
    seen_count = (int) rules.size ();
    if (seen_count > 0)
      {
	auto it = rules.begin ();
	seen_name = it->group_name ();
	seen_semantic = it->semantic ();
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
  if (seen_count != 1)
    __builtin_abort ();
  if (!seen_name || __builtin_strcmp (seen_name, "safety") != 0)
    __builtin_abort ();
  if (seen_semantic != sc::evaluation_semantic::observe)
    __builtin_abort ();
  return 0;
}
