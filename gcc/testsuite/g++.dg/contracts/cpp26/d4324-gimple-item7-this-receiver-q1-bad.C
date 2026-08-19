// Companion to d4324-gimple-item7-this-receiver-q1-ok.C: an unconstrained
// pointer used as the receiver of a member conveyor call. Rejected by
// contracts.cc's own mandatory, unconditional item 7 pass regardless of
// any GIMPLE flag (confirmed by direct testing: the identical error
// fires even with -fcontract-conveyor-proofs-gimple entirely absent) --
// so, exactly like this directory's other item7/item8 violation tests,
// this can only demonstrate "compilation still correctly rejects this
// with both engines active," not GIMPLE's own check in isolation.
// { dg-do compile }
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

struct S {
  int v;
  int bump () conveyor { v = 5; return v; }
};

int via_unasserted_pointer (S *p) conveyor
{
  return p->bump (); // { dg-error "cannot prove .is_object_address. for .p., implicitly required by the receiver" }
}

int main () { return 0; }
