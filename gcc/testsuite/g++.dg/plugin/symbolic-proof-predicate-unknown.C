// symbolic_proof_plugin.cc: f was never established via a call to a
// function whose postcondition asserts is_opened for it -- the plugin
// can't connect this to anything, so the best available answer is
// "cannot verify," not silent acceptance, and not a false claim of a
// proven violation either.  See .claude/plans/well-we-last-discussed-
// ethereal-duckling.md.
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
  void write_data () pre<symbolic_ctrl_v>(is_opened (this)) {}
};

int main ()
{
  io_facility f;
  f.write_data (); // { dg-warning "cannot verify" }
  return 0;
}
