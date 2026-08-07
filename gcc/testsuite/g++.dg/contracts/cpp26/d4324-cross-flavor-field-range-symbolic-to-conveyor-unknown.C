// D4324: one-way trust between the two control-object flavors, for the
// ptr->field range map (m_contract_field_range_map) -- see d4324-cross-
// flavor-predicate-symbolic-to-conveyor-unknown.C for the same rule on
// the named-predicate map.  produce_count_symbolic()'s SYMBOLIC-flavored
// postcondition establishes this->count in [40,100); consume_count_
// conveyor()'s CONVEYOR-flavored precondition requires this->count in
// [20,1000) on the same object -- must report "cannot verify", not
// silently pass, even though [40,100) is a subset of [20,1000): the
// established fact is only backed by symbolic's own, unverified trust,
// never good enough for a conveyor obligation.  See .claude/plans/well-
// we-last-discussed-ethereal-duckling.md.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs -fcontract-symbolic-proofs" }

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

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

struct thing {
  int count;
  void produce_count_symbolic ()
    post<symbolic_ctrl_v>(this->count >= 40 && this->count < 100)
  { count = 55; }
  void consume_count_conveyor ()
    pre<conveyor_ctrl_v>(this->count >= 20 && this->count < 1000)
  { }
};

void caller ()
{
  thing t;
  t.produce_count_symbolic ();
  t.consume_count_conveyor (); // { dg-warning "cannot verify" }
}

int main () { caller (); return 0; }
