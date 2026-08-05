// symbolic_proof_plugin.cc: open_fails()'s postcondition guarantees
// "!is_opened (this)" -- the exact opposite polarity of what
// write_data()'s precondition requires for that same object.  A
// genuine, provable contradiction, discharged via the plugin's own
// query against the real fact engine.  See .claude/plans/well-we-last-
// discussed-ethereal-duckling.md.
// { dg-do compile }
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
  void open_fails () post<symbolic_ctrl_v>(!is_opened (this)) {}
  void write_data () pre<symbolic_ctrl_v>(is_opened (this)) {}
};

void caller ()
{
  io_facility f;
  f.open_fails ();
  f.write_data (); // { dg-error "provably violates the precondition" }
}

int main () { caller (); return 0; }
