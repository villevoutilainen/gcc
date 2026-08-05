// symbolic_proof_plugin.cc: predicate-chaining proof on a callee's own
// return value, OA_PROVEN_FALSE case -- produce_bad's postcondition
// guarantees "!check_it (r)", the exact opposite polarity of what
// consume's precondition requires for that same value.  See
// .claude/plans/well-we-last-discussed-ethereal-duckling.md.
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

bool check_it (int) symbolic;

int produce_bad () post<symbolic_ctrl_v>(r: !check_it (r)) { return -1; }
void consume (int x) pre<symbolic_ctrl_v>(check_it (x)) { (void) x; }

void caller ()
{
  int r = produce_bad ();
  consume (r); // { dg-error "provably violates the precondition" }
}

int main () { caller (); return 0; }
