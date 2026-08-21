// Companion to d4324-conveyor-this-receiver-q1-ok.C: an unconstrained
// pointer/reference used as the receiver of a member conveyor call.
// { dg-do compile { target c++26 } }
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
  int bump () conveyor { v = 5; return v; }
};

int via_unasserted_pointer (S *p) conveyor
{
  return p->bump (); // { dg-error "cannot prove .is_object_address. for .p., required by the precondition" }
}

int main () { return 0; }
