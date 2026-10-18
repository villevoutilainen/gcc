// D4324 (GIMPLE engine also on): call-site proofs see the condition of a
// conditional inside an expression, and a guarded divisor is nonzero, in
// both engines.  The GIMPLE engine now also checks calls to functions that
// are only declared (their contract flavors are cached when a walk meets
// the call), takes the range of a value at the call from the ranger
// rather than a collapsed interval (so 'x != 0' excludes zero), and
// follows a call result through SSA copies to its postcondition range.
// Unprovable obligations are reported by each engine.
//
// Originally: call-site proofs see the condition of a conditional inside an
// expression -- '?:', '&&', '||' -- as for an 'if' statement, the false
// outcome of 'x == 0' makes x nonzero, and a call whose callee's
// postcondition guarantees a range excluding zero ('r > 0') is nonzero.
// An obligation inside a '?:' condition is reported once.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple" }

#include <contracts>

namespace sc = std::contracts;

int divide (int a, int b) conveyor pre<sc::analyzed_conveyor_v> (b != 0);
int pos (int x) conveyor post<sc::conveyor_assert_v> (r: r > 0);
int neg (int x) conveyor post<sc::conveyor_assert_v> (r: r < 0);
int any (int x) conveyor;

int t_if (int x) { if (x != 0) return divide (10, x); return 0; }
int t_cond (int x) { return x != 0 ? divide (10, x) : 0; }
int t_cond_else (int x) { return x == 0 ? 0 : divide (10, x); }
bool t_and (int x) { return x != 0 && divide (10, x) > 1; }
bool t_or (int x) { return x == 0 || divide (10, x) > 1; }
bool t_if_or (int x) { if (x == 0 || divide (10, x) > 1) return true; return false; }
int t_early (int x) { if (x == 0) return 0; return divide (10, x); }
int t_pos (int x) { return divide (10, pos (x)); }
int t_neg (int x) { return divide (10, neg (x)); }
int t_nested (int x, int y) { return x != 0 ? (y != 0 ? divide (x, y) + divide (y, x) : 0) : 0; }

int t_wrong_arm (int x) { return x != 0 ? 0 : divide (10, x); }	// { dg-warning "cannot verify that .x. is nonzero" }
bool t_wrong_and (int x) { return x == 0 && divide (10, x) > 1; }	// { dg-warning "cannot verify that .x. is nonzero" }
int t_any (int x) { return divide (10, any (x)); }			// { dg-warning "cannot verify that .any\\(x\\). is nonzero" }
// { dg-warning "cannot verify that the result of .int any\\(int\\). is nonzero" "" { target *-*-* } .-1 }
int t_in_cond (int x) conveyor { return divide (10, x) ? 1 : 0; }	// { dg-warning "cannot verify that .x. is nonzero" }

int t_div (int a, int b) conveyor { if (b != 0) return a / b; return 0; }
int t_div_un (int a, int b) conveyor { int q = a / b; return q; }	// { dg-error "divisor .b. not provably nonzero" }
