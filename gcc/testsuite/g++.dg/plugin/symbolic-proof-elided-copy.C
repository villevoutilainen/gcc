// symbolic_proof_plugin.cc, as the built-in -fcontract-symbolic-proofs: a
// callee's postcondition about its result is a fact about the object the
// result initializes (guaranteed elision, or a temporary a reference is
// bound to); a by-value class parameter initialized from an object is a
// copy, with none of its facts.  A precondition conjunct with no proof
// rule is reported, not passed over.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
using namespace std::contracts;

struct T { int v; };
bool good (const T &) symbolic;
T make_t () post<never_proven_symbolic_v>(r: good (r));
int by_val (T t) pre<symbolic_assert_v>(good (t));
int by_ref (const T &t) pre<symbolic_assert_v>(good (t));
int plain_size ();
void uncovered (int n) pre<symbolic_assert_v>(n < plain_size ());

int elided () { return by_val (make_t ()); }
int bound () { return by_ref (make_t ()); }
int named () { T s = make_t (); return by_ref (s); }
int copied () { T s = make_t (); return by_val (s); }	// { dg-warning "cannot verify that .bool good" }
void unruled (int n) { uncovered (n); }		// { dg-warning "cannot verify the precondition" }

int main () { return 0; }
