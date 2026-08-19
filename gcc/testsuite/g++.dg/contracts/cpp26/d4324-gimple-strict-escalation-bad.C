// The built-in GIMPLE-pass engine's own strict (proven_conveyor)
// escalation tier: an unprovable conjunct under std::contracts::
// proven_conveyor_v must be a hard error ("cannot prove"), not merely
// a warning ("cannot verify") -- mirroring the AST engine's own strict/
// lenient split (oa_handle_call_conveyor_proof_obligation's own
// 'strict' parameter). Previously this engine had no strict tier at
// all: every unprovable conjunct was a warning regardless of the
// contract's own control object (see .claude/plans/lazy-stirring-
// pearl.md, item 2.7).
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
namespace sc = std::contracts;

int consumer (int n) pre<sc::proven_conveyor_v>(n > 0) { return n; }

int relay (int m)
{
  return consumer (m); // { dg-error "cannot prove that .m. satisfies the precondition" }
}

int main () { return 0; }
