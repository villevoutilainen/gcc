// D4324: a default argument's own resolved expression gets spliced
// into every call site that omits it (convert_default_arg, call.cc)
// without re-running ordinary call resolution for that specific call
// site. caller() has no pre/post/assert/conveyor-declaration of its
// own and never writes 'deref' anywhere in its own source text (only
// 'g ()', omitting the default argument) -- so under the lazy,
// FUNCTION_DECL-tagging detection design (oa_function_needs_walk_p,
// DECL_MIGHT_NEED_OA_SCAN_P) the *only* way caller() can be marked as
// needing the oa_* walk at all is a dedicated touch point right at
// this splice site (oa_mark_fn_if_expr_calls_active_contract, called
// from convert_default_arg itself). Confirmed by direct experiment
// (temporarily disabling that one call) that this test silently,
// wrongly compiles clean without it -- this is a real, load-bearing
// regression test, not merely a plausible one.
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

int deref (int* p) pre<conveyor_ctrl_v>(std::is_object_address(p))
{
  return *p;
}

int* global_ptr;

int g (int x = deref (global_ptr))
{
  return x;
}

int
caller ()
{
  return g (); // { dg-error "cannot prove .is_object_address. for .global_ptr." }
}

int main () { return caller (); }
