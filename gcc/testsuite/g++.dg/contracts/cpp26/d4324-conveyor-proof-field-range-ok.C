// D4324/P2680: -fcontract-conveyor-proofs, ptr->field range proof --
// closes conveyor-proofs' own field-range gap (a conveyor contract's
// 'this->count >= 40 && this->count < 100'-style conjunct previously
// got no scrutiny at all from -fcontract-conveyor-proofs, since
// m_range_map only ever tracks bare decls, never a pointer's own
// field).  produce_count()'s postcondition establishes this->count in
// [40,100); consume_count()'s precondition requires this->count in
// [20,1000) on the same object -- [40,100) is a subset of [20,1000),
// so the obligation is discharged silently, entirely at compile time.
// See .claude/plans/well-we-last-discussed-ethereal-duckling.md.
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
inline constexpr conveyor_ctrl conveyor_ctrl_v{};

struct thing {
  int count;
  void produce_count ()
    post<conveyor_ctrl_v>(this->count >= 40 && this->count < 100)
  { count = 55; }
  void consume_count ()
    pre<conveyor_ctrl_v>(this->count >= 20 && this->count < 1000)
  { }
};

int main ()
{
  thing t;
  t.produce_count ();
  t.consume_count ();
  return 0;
}
