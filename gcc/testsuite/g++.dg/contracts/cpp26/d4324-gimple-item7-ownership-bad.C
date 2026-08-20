// Companion to d4324-gimple-item7-ownership-ok.C: a POINTER parameter's
// own POINTEE, dereferenced back out to another conveyor call's
// non-const reference parameter -- rejected: P's own storage is this
// function's private copy, but *P is still the caller's own, unrelated
// object (matches contracts.cc's own d4324-reference-ownership-basic.C,
// 'reject_ptr_param_dereference'). Q1 for P is established explicitly
// here (an is_object_address precondition), isolating this as a
// genuine Q2 (ownership) violation, not a Q1 one. Gated by
// -fcontract-conveyor-proofs-gimple, run alongside the ordinary
// mandatory AST-level check -- both independently reject this.
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

struct T { int v; };
int use_val_mut (T& x) conveyor { x.v = 5; return x.v; }

int reject_ptr_param_dereference (T *p) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (p))
{
  return use_val_mut (*p); // { dg-error "is not owned by the calling function" }
                           // { dg-message "does not name a parameter or local" "unprovable reason" { target *-*-* } .-1 }
}

int main () { return 0; }
