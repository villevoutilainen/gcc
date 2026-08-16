// D4324: a predicate-returning callee's own postcondition, defining its
// result as a comparison of its own parameter against a literal (e.g.
// 'post<ctrl>(r: r == (v > 0))'), lets its concrete truth be derived at
// any call site from the substituted argument's own known value --
// here a plain literal -- via oa_call_postcondition_predicate_range_p
// (the precondition call-obligation-discharge fallback), with no chain
// through any other established fact required at all. check_it's own
// postcondition uses a plain, non-forced conveyor control object (not
// proven_conveyor): that self-declaration's own conjunct shape ('r ==
// (v > 0)', a boolean-valued definition, not a plain numeric range/
// relation) isn't itself provable by the existing postcondition self-
// check, matching the same "self-check doesn't have a mechanism to
// verify a boolean-valued definition" limitation this whole family of
// helpers works around at the *call site* instead -- forcing check_it's
// own postcondition analysis on would just add unrelated noise here, so
// it stays flag-gated (off) the same way every other test in this suite
// using a plain conveyor predicate does. consume's own precondition
// uses proven_conveyor -- the actual call-obligation-discharge site
// under test here -- so no -fcontract-conveyor-proofs is needed at all.
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

int main ()
{
  consume (5);
  return 0;
}
