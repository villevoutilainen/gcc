// symbolic_proof_plugin.cc checks 'n != 0' and 'std::is_object_address
// (q)' in a symbolic precondition, as the built-in
// -fcontract-symbolic-proofs does: a symbolic postcondition's fact or a
// refinement proves them; nothing known, a warning; zero, a violation.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
using namespace std::contracts;

int div10 (int n) pre<symbolic_assert_v>(n != 0) { return 10 / n; }
void use (int *q) pre<symbolic_assert_v>(std::is_object_address (q)) { (void) q; }
int positive () post<symbolic_assert_v>(r: r > 0) { return 1; }

int nz_unknown (int m) { return div10 (m); }	// { dg-warning "cannot verify that .m. is nonzero" }
int nz_refined (int m) { return m != 0 ? div10 (m) : 0; }
int nz_post () { return div10 (positive ()); }
int nz_zero () { return div10 (0); }		// { dg-error "provably violates the precondition" }
void oa_unknown (int *q) { use (q); }		// { dg-warning "cannot verify .is_object_address. for .q." }
void oa_local () { int i = 0; use (&i); }

int main () { return 0; }
