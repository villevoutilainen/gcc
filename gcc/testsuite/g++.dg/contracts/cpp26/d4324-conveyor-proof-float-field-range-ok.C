// D4324: floating-point field-range tracking -- m_contract_float_field_
// range_map, the (identity, FIELD_DECL)-keyed analogue of m_contract_
// float_range_map, needed for a class wrapping a scalar-float field
// (e.g. 'this->m_value' in a Number-style class). A postcondition
// establishes the field's own range at the call site; a later call's
// precondition consults it -- mirrors the existing integer convention
// (d4324-conveyor-proof-field-range-ok.C) exactly.
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
  double value;
  void produce_value ()
    post<conveyor_ctrl_v>(this->value >= 40.0 // { dg-warning "cannot verify postcondition" }
			  && this->value < 100.0) // { dg-warning "cannot verify postcondition" }
  { value = 55.0; }
  void consume_value ()
    pre<conveyor_ctrl_v>(this->value >= 20.0 && this->value < 1000.0)
  { }
};

int
main ()
{
  thing t;
  t.produce_value ();
  t.consume_value ();
  return 0;
}
