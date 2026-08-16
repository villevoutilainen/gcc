// D4324: same derivation as d4324-conveyor-predicate-postcondition-
// range-literal-ok.C, but the substituted argument's own value comes
// from an established RANGE fact (an earlier if-condition), not a bare
// literal -- confirms oa_call_postcondition_predicate_range_p's own
// reuse of oa_env_check_relational_fact_1 genuinely isn't limited to
// literal-vs-literal comparisons. check_it's own postcondition uses a
// plain, non-forced conveyor control object -- see
// d4324-conveyor-predicate-postcondition-range-literal-ok.C's own
// comment for why.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

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
inline constexpr conveyor_ctrl conveyor_ctrl_v{};

bool
check_it (int const v) conveyor
  post<conveyor_ctrl_v>(r: r == (v > 0))
{
  return v > 0;
}

void
consume (int x) pre<sc::proven_conveyor_v>(check_it (x))
{
}

void
caller (int x)
{
  if (x > 0 && x < 100)
    consume (x);
}

int main () { caller (5); return 0; }
