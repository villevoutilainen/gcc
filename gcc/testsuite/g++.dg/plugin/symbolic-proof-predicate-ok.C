// symbolic_proof_plugin.cc: open()'s postcondition establishes
// is_opened (this) for f; write_data()'s precondition requires exactly
// that same fact on the same object, with nothing invalidating it in
// between.  Proven via the plugin's own query against the real,
// cross-statement-tracked fact engine -- no -fcontract-symbolic-proofs
// needed, since oa_walk_function_calls arms that same substrate on its
// own.  See .claude/plans/well-we-last-discussed-ethereal-duckling.md.
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
namespace sc = std::contracts;

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

struct io_facility {
  static bool is_opened (io_facility*) symbolic;
  void open () post<symbolic_ctrl_v>(is_opened (this)) {}
  void write_data () pre<symbolic_ctrl_v>(is_opened (this)) {}
};

int main ()
{
  io_facility f;
  f.open ();
  f.write_data ();
  return 0;
}
