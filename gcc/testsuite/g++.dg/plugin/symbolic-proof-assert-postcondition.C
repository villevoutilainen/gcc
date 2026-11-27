// symbolic_proof_plugin.cc checks a symbolic contract_assert, and a
// function's own symbolic postcondition against its body, as the
// built-in -fcontract-symbolic-proofs does; a conjunct of a
// postcondition naming a symbolic relation is an axiom, and a
// never_proven assertion is trusted as written.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
using namespace std::contracts;

struct Node { int v; };
bool is_linked (const Node *, const Node *) symbolic;
void link (Node *const a, Node *const b) post<never_proven_symbolic_v>(is_linked (a, b));
void touch (Node *);

void asserted (Node *a, Node *b) { link (a, b); contract_assert<symbolic_assert_v>(is_linked (a, b)); }
void not_asserted (Node *a, Node *b) { link (a, b); touch (b); contract_assert<symbolic_assert_v>(is_linked (a, b)); } // { dg-warning "cannot verify 'contract_assert' condition" }
void trusted (Node *a, Node *b) { contract_assert<never_proven_symbolic_v>(is_linked (a, b)); }

void axiom (Node *const a, Node *const b) post<symbolic_assert_v>(is_linked (a, b)) { }
int positive () post<symbolic_assert_v>(r: r > 0) { return 1; }
int negative () post<symbolic_assert_v>(r: r > 0) { return -1; } // { dg-error "postcondition condition .* is provably false" }

int main () { return 0; }
