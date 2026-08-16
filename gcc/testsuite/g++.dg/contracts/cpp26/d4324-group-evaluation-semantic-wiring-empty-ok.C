// D4324: with no -fcontracts-group-evaluation-semantic= at all (the
// common case), assertion_static_info::group_semantic_rules() must be
// a genuinely empty, harmless view -- no zero-length-array machinery
// leaking into every ordinary assertion's static info.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
namespace sc = std::contracts;

struct probe_ctrl {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  {
    auto rules = ctx.static_info ().group_semantic_rules ();
    if (rules.size () != 0)
      __builtin_abort ();
    if (rules.begin () != rules.end ())
      __builtin_abort ();
    if (!ctx.check ())
      __builtin_abort ();
  }
};
inline constexpr probe_ctrl probe_v{};

int f (int x) pre<probe_v>(x > 0) { return x; }

int
main ()
{
  return f (5) - 5;
}
