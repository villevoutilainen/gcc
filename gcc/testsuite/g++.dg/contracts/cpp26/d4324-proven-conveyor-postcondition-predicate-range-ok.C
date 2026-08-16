// D4324: the postcondition self-check (oa_handle_postcondition_stmt)
// consulting a named-predicate conjunct now also benefits from the new
// call-obligation-discharge fallback (oa_call_postcondition_predicate_
// range_p): produce()'s own claim 'check_it(r)' was previously always
// "cannot verify" regardless of check_it's own contract (see
// d4324-conveyor-proof-predicate-ok.C, still correctly unprovable
// there since check_it has no contract of its own) -- here check_it
// DOES declare its own result via a comparison of its argument, so
// produce()'s own return value (a literal, known at its own return
// point) makes the claim genuinely, statically true. check_it's own
// postcondition uses a plain, non-forced conveyor control object --
// see d4324-conveyor-predicate-postcondition-range-literal-ok.C's own
// comment for why (its own conjunct shape isn't itself provable by
// this same self-check, and forcing it on would just add unrelated
// noise here).
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

int
produce ()
  post<sc::proven_conveyor_v>(r: check_it (r))
{
  return 1;
}

int main () { return produce () > 0 ? 0 : 1; }
