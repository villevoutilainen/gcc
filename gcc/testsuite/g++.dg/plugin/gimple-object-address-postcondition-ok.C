// gimple_object_address_plugin.cc: item 6's own shape -- make_ptr()'s
// declared postcondition unconditionally guarantees is_object_address
// for its own *return value*, read declaratively off make_ptr()'s own
// FUNCTION_DECL (call_postcondition_guarantees_object_address_p), with
// no dependence on whether make_ptr()'s own GIMPLE body has been
// visited by this pass yet. provable_object_address_p recognizes 'p's
// own def-stmt as exactly this shape (a GIMPLE_CALL to a callee whose
// postcondition names its own result identifier), so deref(p)'s own
// obligation is discharged purely from that declared guarantee -- the
// same conclusion the existing, mandatory AST-level check already
// reaches (compare gcc/testsuite/g++.dg/contracts/cpp26/d4324-object-
// address-postcondition-ok.C). See ~/gimple-contract-analysis.md.
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
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

int a;

int* make_ptr () post<conveyor_ctrl_v>(r: std::is_object_address (r))
{
  return &a;
}

int deref (int* p) pre<conveyor_ctrl_v>(std::is_object_address (p))
{
  return *p;
}

int f ()
{
  int *p = make_ptr ();
  return deref (p);
}

int main () { a = 5; return f () - 5; }
