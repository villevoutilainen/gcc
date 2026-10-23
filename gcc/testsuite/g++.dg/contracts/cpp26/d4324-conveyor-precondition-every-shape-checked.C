// D4324: every conjunct of a conveyor callee's precondition is checked at
// the call, whatever its shape.  A '!=' against something other than a
// bare zero, a reference parameter's '!= 0', a disjunction, a 'bool'
// parameter, a call compared with a constant: each used to be passed
// over silently, as if proven.  Now each is proven, proven violated, or
// reported as unprovable -- folded with the arguments when they are
// constants.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
using std::contracts::conveyor_assert_v;

int g (int) conveyor;
void ne1 (int x) conveyor pre<conveyor_assert_v> (x != 1);
void ner (int &x) conveyor pre<conveyor_assert_v> (x != 1);
void nzr (int &x) conveyor pre<conveyor_assert_v> (x != 0);
void nzc (const int &x) conveyor pre<conveyor_assert_v> (x != 0);
void off (int x) conveyor pre<conveyor_assert_v> (x > 0 && x < 1000 && x - 2 != 1);
void two (int x, int y) conveyor pre<conveyor_assert_v> (x != y);
void alt (int x) conveyor pre<conveyor_assert_v> (x == 3 || x == 5);
void flag (bool b) conveyor pre<conveyor_assert_v> (b);
void callc (int x) conveyor pre<conveyor_assert_v> (g (x) != 2);
void deref (int *p) conveyor
  pre<conveyor_assert_v> (std::is_object_address (p) && *p != 0);

// Proven.
void p1 (int k) conveyor { if (k > 5) ne1 (k); }
void p2 () conveyor { ne1 (7); }
void p3 () conveyor { int l = 5; ner (l); }
void p4 () conveyor { int l = 3; nzr (l); }
void p5 (int k) conveyor { if (k > 0 && k < 9) nzr (k); }
void p6 (int k) conveyor { if (k > 3 && k < 100) off (k); }
void p7 () conveyor { two (1, 2); alt (5); flag (true); }

// Provably violated.
void v1 () conveyor { ne1 (1); }		// { dg-error "argument .1. provably violates" }
void v2 () conveyor { int l = 1; ner (l); }	// { dg-error "argument .l. provably violates" }
void v3 () conveyor { int l = 0; nzr (l); }	// { dg-error "argument .l. provably violates" }
void v4 () conveyor { two (2, 2); }		// { dg-error "provably violate" }
void v5 () conveyor { alt (4); }		// { dg-error "the arguments provably violate the precondition" }
void v6 () conveyor { flag (false); }		// { dg-error "the arguments provably violate the precondition" }

// Unprovable.
void u1 (int k) conveyor { ne1 (k); }		// { dg-error "cannot prove that .k. satisfies" }
void u2 (int k) conveyor { ner (k); }		// { dg-error "cannot prove that .k. satisfies" }
void u3 (int k) conveyor { nzr (k); }		// { dg-error "cannot prove that .k." }
void u4 (int k) conveyor { nzc (k); }		// { dg-error "cannot prove that .k." }
void u5 (int k, int j) conveyor { two (k, j); }	// { dg-error "cannot prove" }
void u6 (int k) conveyor { alt (k); }		// { dg-error "cannot prove the precondition" }
void u7 (bool k) conveyor { flag (k); }		// { dg-error "cannot prove the precondition" }
void u8 (int k) conveyor { callc (k); }		// { dg-error "cannot prove" }
void u9 (int k) conveyor { deref (&k); }	// { dg-error "cannot prove" }

// A condition excluding a value ('k != 1', or 'k == 1' false) proves
// '!= 1' for as long as the value is unchanged.
int give () conveyor;
void touch (int &) conveyor;
void touchp (int *) conveyor;
void e1 (int k) conveyor { if (k != 1) ne1 (k); }
void e2 (int k) conveyor { if (k == 1) return; ne1 (k); }
void e3 (int k) conveyor { if (!(k == 1)) ne1 (k); }
void e4 (int k) conveyor { if (k != 1 && k > 0) ne1 (k); }
void e5 (int k) conveyor { if (k > 0 && k != 1) ne1 (k); }
void e6 (int k) conveyor { if (k != 1) ner (k); }
void e7 (int k, bool c) conveyor { if (k != 1) { if (c) ne1 (k); else ne1 (k); } }
void e8 (int k) conveyor { while (k != 1) { ne1 (k); k = give (); } }
void x1 (int k) conveyor { if (k != 1) { k = give (); ne1 (k); } }	// { dg-error "cannot prove that .k. satisfies" }
void x2 (int k) conveyor { if (k != 1 && k > -9 && k < 9) { ++k; ne1 (k); } }		// { dg-error "cannot prove that .k. satisfies" }
void x3 (int k) conveyor { if (k != 1 && k > -9 && k < 9) { k += 2; ne1 (k); } }		// { dg-error "cannot prove that .k. satisfies" }
void x4 (int k) conveyor { if (k != 1) { touch (k); ne1 (k); } }	// { dg-error "cannot prove that .k. satisfies" }
void x5 (int k) conveyor { if (k != 1) { touchp (&k); ne1 (k); } }	// { dg-error "cannot prove that .k. satisfies" }
void x6 (int k, bool c) conveyor { if (c) { if (k == 1) return; } ne1 (k); } // { dg-error "cannot prove that .k. satisfies" }
void x7 (int k) conveyor { if (k != 2) ne1 (k); }			// { dg-error "cannot prove that .k. satisfies" }
void x8 (int k) conveyor { if (k != 1 && k > -9 && k < 9) { k--; ne1 (k); } }		// { dg-error "cannot prove that .k. satisfies" }

// { dg-prune-output "no proof rule covers a condition of this form" }
// { dg-prune-output "no fact relating this value" }
// { dg-prune-output "a numeric range is known" }
