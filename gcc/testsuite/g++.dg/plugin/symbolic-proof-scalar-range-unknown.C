// symbolic_proof_plugin.cc: untrusted was never established via a call
// to a function whose postcondition asserts a range for its own
// result -- the plugin can't connect this to anything, so the best
// available answer is "cannot verify," not silent acceptance, and not
// a false claim of a proven violation either.  See .claude/plans/well-
// we-last-discussed-ethereal-duckling.md.
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

void consumer (int x) pre<symbolic_ctrl_v>(x >= 20 && x < 100) { (void) x; }

void caller (int untrusted)
{
  consumer (untrusted); // { dg-warning "cannot verify" }
}

int main () { caller (1); return 0; }
