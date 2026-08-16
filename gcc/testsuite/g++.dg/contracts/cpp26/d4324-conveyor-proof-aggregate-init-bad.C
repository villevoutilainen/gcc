// D4324/P2680: companion to d4324-conveyor-proof-aggregate-init-ok.C --
// p.x's established value (200.0) is genuinely outside consume()'s
// required [0,100], a genuine, provable violation, caught purely from
// the aggregate's own brace-init literal.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct Point {
  double x;
  double y;
};

void consume (Point p)
  pre<sc::proven_conveyor_v>(p.x >= 0.0 && p.x <= 100.0)
{ }

int main ()
{
  Point p = { 200.0, 2.0 };
  consume (p); // { dg-error "provably violates the precondition" }
  return 0;
}
