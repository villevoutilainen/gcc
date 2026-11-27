// symbolic_proof_plugin.cc checks a symbolic relation of several objects
// ('is_linked (a, b)') in a callee's precondition, as the built-in
// -fcontract-symbolic-proofs does: established for the same arguments in
// order, proven; for others, or after a call that may change one of
// them, unverifiable; of the other polarity, provably failed.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
using namespace std::contracts;

struct Node { int v; };
bool is_linked (const Node *, const Node *) symbolic;
void link (Node *const a, Node *const b) post<never_proven_symbolic_v>(is_linked (a, b));
void walk (const Node *a, const Node *b) pre<symbolic_assert_v>(is_linked (a, b));
void apart (const Node *a, const Node *b) pre<symbolic_assert_v>(!is_linked (a, b));
void walk_s (const Node *a, const Node *b) pre<proven_symbolic_v>(is_linked (a, b));
void touch (Node *);

void proven (Node *a, Node *b) { link (a, b); walk (a, b); }
void swapped (Node *a, Node *b) { link (a, b); walk (b, a); }	// { dg-warning "cannot verify that .bool is_linked" }
void touched (Node *a, Node *b) { link (a, b); touch (a); walk (a, b); } // { dg-warning "cannot verify" }
void strict (Node *a, Node *b) { walk_s (a, b); }		// { dg-error "cannot prove that .bool is_linked" }
void failed (Node *a, Node *b) { link (a, b); apart (a, b); }	// { dg-error "provably fails the precondition" }

int main () { return 0; }
