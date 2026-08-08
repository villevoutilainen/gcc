// gimple_object_address_plugin.cc: a deliberately out-of-scope shape for
// this narrow prototype -- make_ptr()'s own postcondition guarantees
// is_object_address for its *return value* (item 6's own mechanism),
// which the existing, mandatory AST-level check already understands
// (and so accepts 'deref (p)' below silently, where p was assigned
// from that call) -- but this prototype's own provable_object_address_p
// only chases GIMPLE_PHI/GIMPLE_ASSIGN def-stmts, not a GIMPLE_CALL's
// own return-value guarantee (see the plugin's own top-of-file comment,
// "no attempt at ... item-6 ... call substitution"), so it reports
// "cannot verify" here even though the construct is actually sound.
// This is an honest, expected divergence from the full engine given
// this prototype's own documented scope, not a soundness bug in either
// engine (this plugin's own limitation only ever produces an extra
// warning, never a false accept) -- see ~/gimple-contract-analysis.md.
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
  return deref (p); // { dg-warning "gimple-oa: cannot verify" }
}

int main () { a = 5; return f () - 5; }
