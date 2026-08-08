// The built-in GIMPLE-pass engine's own invalidation case for
// field-range facts (-fcontract-conveyor-proofs-gimple): an
// intervening call to an ordinary, uncontracted function
// ('unrelated(&t)') receiving t's own address between produce() and
// consume() must invalidate the this->count fact produce()
// established, since there is no way to know unrelated() didn't
// change it (cg_invalidate_persistent_facts_for_call_args drops every
// tracked field for the same identity, matching contracts.cc's own
// contract_field_range_invalidate_all's whole-object granularity).
// See gcc/cp/contracts-gimple.cc and ~/gimple-contract-analysis.md.
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

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

struct thing {
  int count;
  void produce () post<conveyor_ctrl_v>(this->count >= 40 && this->count < 100)
  { count = 55; }
  void consume () pre<conveyor_ctrl_v>(this->count >= 20 && this->count < 1000)
  { }
};

void unrelated (thing *) {}

void invalidated_caller ()
{
  thing t;
  t.produce ();
  unrelated (&t);
  t.consume (); // { dg-warning "cannot verify that field .*count.*satisfies" }
}

int main () { invalidated_caller (); return 0; }
