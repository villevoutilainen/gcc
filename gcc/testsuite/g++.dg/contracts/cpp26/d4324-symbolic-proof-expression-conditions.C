// D4324: the symbolic flavor of d4324-conveyor-proof-expression-conditions.C.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs" }

#include <contracts>

namespace sc = std::contracts;

int divide (int a, int b) pre<sc::analyzed_symbolic_v> (b != 0);
int pos (int x) post<sc::symbolic_assert_v> (r: r > 0);
int neg (int x) post<sc::symbolic_assert_v> (r: r < 0);
int any (int x);

int t_if (int x) { if (x != 0) return divide (10, x); return 0; }
int t_cond (int x) { return x != 0 ? divide (10, x) : 0; }
int t_cond_else (int x) { return x == 0 ? 0 : divide (10, x); }
bool t_and (int x) { return x != 0 && divide (10, x) > 1; }
bool t_or (int x) { return x == 0 || divide (10, x) > 1; }
int t_early (int x) { if (x == 0) return 0; return divide (10, x); }
int t_pos (int x) { return divide (10, pos (x)); }
int t_neg (int x) { return divide (10, neg (x)); }

int t_wrong_arm (int x) { return x != 0 ? 0 : divide (10, x); }	// { dg-error "argument .x. provably violates" }
bool t_wrong_and (int x) { return x == 0 && divide (10, x) > 1; }	// { dg-error "argument .x. provably violates" }
int t_any (int x) { return divide (10, any (x)); }			// { dg-warning "cannot verify that .any\\(x\\). is nonzero" }
