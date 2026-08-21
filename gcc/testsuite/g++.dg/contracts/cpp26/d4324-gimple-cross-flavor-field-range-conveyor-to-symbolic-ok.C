// The built-in GIMPLE-pass engine's own one-way trust between the two
// control-object flavors, for the ptr->field range map (cg_field_
// fact's own CONVEYOR_ESTABLISHED) -- see d4324-gimple-cross-flavor-
// field-range-symbolic-to-conveyor-unknown.C for the forbidden
// direction. produce_count_conveyor()'s CONVEYOR-flavored postcondition
// establishes this->count in [40,100); consume_count_symbolic()'s
// SYMBOLIC-flavored precondition requires this->count in [20,1000) on
// the same object -- a conveyor-established fact is trustworthy enough
// for symbolic's own check to rely on, so this is discharged silently.
// Mirrors the AST-walk's own d4324-cross-flavor-field-range-conveyor-
// to-symbolic-ok.C. See gcc/cp/contracts-gimple.cc and
// ~/gimple-contract-analysis.md.
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple -fcontract-symbolic-proofs-gimple" }
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

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

struct thing {
  int count;
  void produce_count_conveyor ()
    pre<conveyor_ctrl_v>(std::is_object_address (this))
    post<conveyor_ctrl_v>(this->count >= 40 && this->count < 100)
  { count = 55; }
  void consume_count_symbolic ()
    pre<symbolic_ctrl_v>(std::is_object_address (this))
    pre<symbolic_ctrl_v>(this->count >= 20 && this->count < 1000)
  { }
};

int main ()
{
  thing t;
  t.produce_count_conveyor ();
  t.consume_count_symbolic ();
  return 0;
}
