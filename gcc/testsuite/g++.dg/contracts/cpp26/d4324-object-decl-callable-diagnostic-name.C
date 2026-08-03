// D4324: a contract-violation message for a declaration-level pre<>/
// post<> clause on a callable-typed OBJECT declaration (see
// .claude/plans/stateless-jumping-shore.md) must name the contracted
// object itself (e.g. "divide") in its "in function ..." text -- not
// the compiler-synthesized internal check function
// (build_object_contract_check_function's own
// "__contract_post_check_N"), which a user has no way to recognize or
// connect back to their own code.  Mirrors
// basic.contract.eval.p11-observe.C's own dg-output style, for the
// ordinary-function case this feature is built on top of.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-evaluation-semantic=observe" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

int (*divide) (int a, int b) pre (b != 0) post (r: r >= 0);

int real_divide (int a, int b) { return a / b; }

int main ()
{
  divide = real_divide;
  divide (6, -1);
  return 0;
}
// { dg-output "contract violation in function divide at .*: r >= 0.*(\n|\r\n|\r)" }
// { dg-output ".assertion_kind: post, semantic: observe, mode: predicate_false, terminating: no.*(\n|\r\n|\r)" }
