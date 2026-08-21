// D4324/P2680 soundness fix (see ~/soundness-fixes-for-conveyors.md):
// std::is_object_address(this) is NEVER an unconditional axiom -- 'this'
// needs exactly the same proof any other pointer/reference does. A
// conveyor-declared function's own 'this' gets that proof for free (a
// compiler-synthesized implicit precondition, discharged by its own
// callers -- oa_synthesize_implicit_reference_safety_preconditions); an
// ORDINARY function like CHECK below carries no such precondition, so
// it must give itself an explicit one before its own body can assert
// is_object_address(this) at all.
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

struct S {
  int v;
  void check ()
    pre<conveyor_ctrl_v>(std::is_object_address (this))
  { contract_assert<conveyor_ctrl_v>(std::is_object_address(this)); }
};

int main () { S s{1}; s.check (); return 0; }
