// D4324: a diagnostic for a strict contract on a function-pointer (or
// pointer-to-member-function) declaration names the declarator's own
// parameter, not a synthesized '__a0' (P4445 BUG-08).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

int div_by (int x, int d) conveyor
  pre<std::contracts::proven_conveyor_v>(d != 0) { return x / d; }

void f (int) {}
void (*p) (int x)
  pre<std::contracts::conveyor_assert_v>(div_by (1, x) > 0) = f; // { dg-error "cannot prove that 'x' is nonzero" }

void g (int, int) {}
void (*q) (int, int y)
  pre<std::contracts::conveyor_assert_v>(div_by (1, y) > 0) = g; // { dg-error "cannot prove that 'y' is nonzero" }

struct S { void m (int); };
void (S::*pm) (int z)
  pre<std::contracts::conveyor_assert_v>(div_by (1, z) > 0) = &S::m; // { dg-error "cannot prove that 'z' is nonzero" }
