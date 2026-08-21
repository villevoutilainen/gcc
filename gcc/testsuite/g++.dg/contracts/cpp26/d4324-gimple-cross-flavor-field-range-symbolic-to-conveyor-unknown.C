// The built-in GIMPLE-pass engine's own one-way trust between the two
// control-object flavors, for the ptr->field range map --
// produce_count_symbolic()'s SYMBOLIC-flavored postcondition
// establishes this->count in [40,100); consume_count_conveyor()'s
// CONVEYOR-flavored precondition requires this->count in [20,1000) on
// the same object -- must report "cannot verify", not silently pass,
// even though [40,100) is a subset of [20,1000): the established fact
// is only backed by symbolic's own, unverified trust, never good
// enough for a conveyor obligation. Mirrors the AST-walk's own d4324-
// cross-flavor-field-range-symbolic-to-conveyor-unknown.C. See
// gcc/cp/contracts-gimple.cc and ~/gimple-contract-analysis.md.
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
  void produce_count_symbolic ()
    pre<symbolic_ctrl_v>(std::is_object_address (this))
    post<symbolic_ctrl_v>(this->count >= 40 && this->count < 100)
  { count = 55; }
  void consume_count_conveyor ()
    pre<conveyor_ctrl_v>(std::is_object_address (this))
    pre<conveyor_ctrl_v>(this->count >= 20 && this->count < 1000)
  { }
};

void caller ()
{
  thing t;
  t.produce_count_symbolic ();
  t.consume_count_conveyor (); // { dg-warning "cannot verify that field .*count.*satisfies" }
                                // { dg-message "weaker .non-conveyor. trust" "unprovable reason" { target *-*-* } .-1 }
}

int main () { caller (); return 0; }
