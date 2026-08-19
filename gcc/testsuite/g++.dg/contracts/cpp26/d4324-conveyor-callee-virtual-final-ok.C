// D4324: a call through a 'final'-marked override is statically
// devirtualized (LOOKUP_NONVIRTUAL gets set before the callee-must-be-
// conveyor check runs), so it's treated as an ordinary call, not banned
// as virtual -- and, since the resolved target is itself declared
// 'conveyor', the call is legal.
//
// See d4324-conveyor-callee-virtual-bad.C's own comment for why the
// destructors here are deliberately left non-conveyor.
// See .claude/plans/well-we-last-discussed-ethereal-duckling.md.
//
// P2680 author correction (2026-08-19): a member conveyor call's own
// receiver now carries the same implicit is_object_address obligation
// an explicit reference parameter always has (see oa_handle_call_
// precondition_obligation's own is_this_parameter block) -- 'd', G's
// own PLAIN POINTER parameter, is exactly as opt-in as it would be
// passing '*d' to an explicit reference parameter elsewhere (Q1 has
// always been opt-in-only for pointers), so G now needs this explicit
// precondition to keep compiling; that requirement is orthogonal to
// this test's own actual point (final/virtual devirtualization), which
// is otherwise unaffected.
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

struct Base
{
  virtual int f (int x) conveyor { return x; }
  virtual ~Base () {}
};

struct Derived final : Base
{
  int f (int x) conveyor override { return x; }
};

int g (Derived *d) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (d))
{
  return d->f (1);
}

int main () { Derived d; return g (&d) - 1; }
