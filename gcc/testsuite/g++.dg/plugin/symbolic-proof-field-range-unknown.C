// symbolic_proof_plugin.cc: t's count field was never established via
// a call to a function whose postcondition asserts a range for it --
// the plugin can't connect this to anything, so the best available
// answer is "cannot verify," not silent acceptance, and not a false
// claim of a proven violation either.  See .claude/plans/well-we-last-
// discussed-ethereal-duckling.md.
//
// Deliberately a plain, undecorated function call (get_count()), not a
// literal: a direct field write from a literal ('t.count = 0;') is NOT
// actually unprovable any more -- oa_walk_stmt's own field-write
// establishment (see "D4324: field-write self-check facts +
// compound-expression preconditions") establishes a real field-range
// fact straight from a literal RHS, which this test originally used and
// which a later regression sweep found silently made this exact case
// provably FALSE (a hard error) instead of unknown. get_count() has no
// postcondition of its own, so oa_get_range has nothing to establish a
// fact from (deliberately not a matter of what the function's body
// actually computes -- the engine never looks at an ordinary function's
// body, only its own declared contract, matching every conveyor-
// declared callee's "trusted by construction" treatment elsewhere in
// this engine), restoring the genuinely-unprovable scenario this test
// is actually about.
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

int get_count () { return 0; }

struct thing {
  int count;
  void consume_count ()
    pre<symbolic_ctrl_v>(this->count >= 20 && this->count < 100)
  { }
};

int main ()
{
  thing t;
  t.count = get_count ();
  t.consume_count (); // { dg-warning "cannot verify" }
  return 0;
}
