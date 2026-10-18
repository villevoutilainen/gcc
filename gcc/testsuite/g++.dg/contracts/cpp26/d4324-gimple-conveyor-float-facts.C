// D4324: the GIMPLE engine keeps floating-point facts that don't come
// from branches, like the AST engine: a precondition on a parameter, a
// value stored to a field, a postcondition about a field seen by a
// caller, and a literal argument checked against a callee's
// precondition.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple -fcontract-conveyor-proofs" }

#include <contracts>
using namespace std::contracts;

double param (double x) pre<conveyor_assert_v> (x >= 1.0)
{
  contract_assert<conveyor_assert_v> (x > 0.5);
  contract_assert<conveyor_assert_v> (x < 0.0); // { dg-error "is provably false" }
  return x;
}

struct number
{
  double value;
  void produce () post<conveyor_assert_v> (this->value >= 40.0 && this->value < 100.0)
  { value = 55.0; }
  void consume () pre<conveyor_assert_v> (this->value >= 20.0 && this->value < 1000.0) {}
  void too_small () pre<conveyor_assert_v> (this->value < 10.0) {}
};

void take_percentage (double p) pre<conveyor_assert_v> (p >= 0.0 && p <= 100.0) {}

void calls ()
{
  number n;
  n.produce ();
  n.too_small (); // { dg-error "provably violates the precondition" }
  // { dg-message "is established \[^\n\]*, but the precondition requires" "" { target *-*-* } .-1 }
  n.produce ();
  n.consume ();
  n.consume (); // { dg-warning "cannot verify" "consume may change the field" }
  take_percentage (50.0);
  take_percentage (150.0); // { dg-error "provably violates the precondition" }
  // { dg-message "is established \[^\n\]*, but the precondition requires" "" { target *-*-* } .-1 }
}
