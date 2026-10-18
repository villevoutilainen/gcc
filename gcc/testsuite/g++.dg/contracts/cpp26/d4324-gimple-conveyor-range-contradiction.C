// D4324: the GIMPLE engine reports a call whose argument's established
// range -- from a callee's postcondition, or a field's fact -- can't
// satisfy the precondition at all as provably violating it, like the AST
// engine, rather than as merely unverified.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple -fcontract-conveyor-proofs" }

#include <contracts>
using namespace std::contracts;

int negative () post<conveyor_assert_v> (r: r < 0) { return -1; }
int forty_to_ninety () post<conveyor_assert_v> (r: r >= 40 && r < 100) { return 50; }
void positive (int x) pre<conveyor_assert_v> (x > 0) {}
void small (int x) pre<conveyor_assert_v> (x >= 0 && x < 10) {}
void wide (int x) pre<conveyor_assert_v> (x >= 0 && x < 1000) {}
void middling (int x) pre<conveyor_assert_v> (x >= 60) {}

struct thing
{
  int count;
  void produce () post<conveyor_assert_v> (this->count >= 200 && this->count < 300) { count = 250; }
  void consume () pre<conveyor_assert_v> (this->count >= 20 && this->count < 100) {}
};

void calls ()
{
  positive (negative ()); // { dg-error "provably violates the precondition" }
  // { dg-message "is established \[^\n\]*, but the precondition requires" "" { target *-*-* } .-1 }
  small (forty_to_ninety ()); // { dg-error "provably violates the precondition" }
  // { dg-message "is established \[^\n\]*, but the precondition requires" "" { target *-*-* } .-1 }
  wide (forty_to_ninety ());
  middling (forty_to_ninety ()); // { dg-warning "cannot verify" }
  thing t;
  t.produce ();
  t.consume (); // { dg-error "provably violates the precondition" }
  // { dg-message "is established \[^\n\]*, but the precondition requires" "" { target *-*-* } .-1 }
}
