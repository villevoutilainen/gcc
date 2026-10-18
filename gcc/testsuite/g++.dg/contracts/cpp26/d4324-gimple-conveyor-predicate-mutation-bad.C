// D4324: the GIMPLE engine rejects a conveyor contract condition that
// modifies something its own evaluation didn't create, like the AST
// engine: an assignment, increment or decrement of a parameter of the
// guarded function, or of a field of one.  A local of the condition's
// own (here an immediately invoked lambda's) is fine.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>
using namespace std::contracts;

struct S { int v; };

void assign_ref (int &r) pre<conveyor_assert_v> ((r = 5, true)) {} // { dg-error "assignment to .r. not permitted in a conveyor predicate" }
void assign_value (int x) pre<conveyor_assert_v> ((x = 5, true)) {} // { dg-error "assignment to .x. not permitted in a conveyor predicate" }
void increment (int &r) pre<conveyor_assert_v> (++r > 0) {} // { dg-error "increment of .r. not permitted in a conveyor predicate" }
// { dg-error "increment of .r. not provably free of overflow" "" { target *-*-* } .-1 }
void decrement (int &r) pre<conveyor_assert_v> (r-- > 0) {} // { dg-error "decrement of .r. not permitted in a conveyor predicate" }
// { dg-error "decrement of .r. not provably free of overflow" "" { target *-*-* } .-1 }
void field (S &s) pre<conveyor_assert_v> ((s.v = 1, true)) {} // { dg-error "assignment to .s.S::v. not permitted in a conveyor predicate" }
void own_local (int x) pre<conveyor_assert_v> ([] (int y) { y = y / 2; return y >= 0 || y < 0; } (x)) {}
void not_conveyor (int &r) pre ((r = 5, true)) {}

int main ()
{
  int i = 0;
  S s{0};
  assign_ref (i); assign_value (1); increment (i); decrement (i);
  field (s); own_local (1); not_conveyor (i);
}
