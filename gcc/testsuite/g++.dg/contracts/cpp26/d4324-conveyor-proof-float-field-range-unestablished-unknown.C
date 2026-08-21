// D4324: a field precondition using a REAL_CST bound must genuinely be
// checked (and decline as "cannot verify" when the field's own range is
// unestablished), not silently skipped as if it were never written --
// oa_symbolic_comparison_conjunct_shape (the shared base matcher every
// field-range function relies on) required INTEGER_CST at its own core
// matching step, so a REAL_CST-bounded field conjunct never even
// reached the collection/consult logic and produced no diagnostic at
// all -- confirmed by direct testing before the fix, distinct from a
// sound decline.
// { dg-do compile { target c++26 } }
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
  double value;
  void consume_value ()
    pre<conveyor_ctrl_v>(std::is_object_address (this))
    pre<conveyor_ctrl_v>(this->value >= 20.0 && this->value < 1000.0)
  { }
};

int
main ()
{
  thing t;
  t.consume_value (); // { dg-warning "cannot verify" }
  return 0;
}
