// D4324: a symbolic predicate relating several objects (pointer or
// reference parameters): a fact keyed by the predicate and its
// arguments' identities, in order, ending when any of them may change.
// The ast engine.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs -fcontract-analysis-engine=ast" }

#include <contracts>
using namespace std::contracts;

struct Node { int v; };
bool is_linked (const Node *, const Node *) symbolic;
void link (Node *const a, Node *const b) post<never_proven_symbolic_v>(is_linked (a, b));
void unlink (Node *const a, Node *const b) post<never_proven_symbolic_v>(!is_linked (a, b));
void walk (const Node *a, const Node *b) pre<symbolic_assert_v>(is_linked (a, b));
void apart (const Node *a, const Node *b) pre<symbolic_assert_v>(!is_linked (a, b));
void walk_s (const Node *a, const Node *b) pre<proven_symbolic_v>(is_linked (a, b));
void touch (Node *);
void look (const Node *);

void o1 (Node *a, Node *b) { link (a, b); walk (a, b); }
void o2 (Node *a, Node *b) { link (a, b); walk (b, a); }	// { dg-warning "cannot verify that .bool is_linked" }
void o3 (Node *a, Node *b) { link (a, b); touch (a); walk (a, b); } // { dg-warning "cannot verify" }
void o4 (Node *a, Node *b) { link (a, b); touch (b); walk (a, b); } // { dg-warning "cannot verify" }
void o5 (Node *a, Node *b) { walk (a, b); }			// { dg-warning "cannot verify" }
void o6 (Node *a, Node *b) { walk_s (a, b); }			// { dg-error "cannot prove that .bool is_linked" }
void o7 (Node *a, Node *b) { link (a, b); contract_assert<symbolic_assert_v>(is_linked (a, b)); }
void o8 (Node *a, Node *b) { link (a, b); apart (a, b); }	// { dg-error "provably fails the precondition" }
void o9 (Node *a, Node *b) { unlink (a, b); apart (a, b); }
void o10 (Node *a, Node *b, bool c) { if (c) link (a, b); else link (a, b); walk (a, b); }
void o11 (Node *a, Node *b, bool c) { if (c) link (a, b); walk (a, b); } // { dg-warning "cannot verify" }
void o12 (Node *a, Node *b, bool c) { link (a, b); if (c) unlink (a, b); walk (a, b); } // { dg-warning "cannot verify" }
void o13 (Node *a, Node *b) { contract_assert<never_proven_symbolic_v>(is_linked (a, b)); walk (a, b); }
void o14 (const Node *a, const Node *b) pre<symbolic_assert_v>(is_linked (a, b)) { walk (a, b); }
void o15 (Node *a, Node *b) { link (a, b); walk_s (a, b); }
void o16 (Node *a, Node *b, Node *c) { link (a, b); walk (a, c); } // { dg-warning "cannot verify" }
void o17 (Node *a, Node *b) { link (a, b); look (a); walk (a, b); } // { dg-warning "cannot verify" }
Node *ret_ok (Node *const a, Node *const b) post<symbolic_assert_v>(is_linked (a, b)) { link (a, b); return a; }
