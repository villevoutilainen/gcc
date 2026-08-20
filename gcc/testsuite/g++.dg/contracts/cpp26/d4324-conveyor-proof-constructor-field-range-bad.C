// D4324/P2680: companion to d4324-conveyor-proof-constructor-field-
// range-ok.C -- the constructor's own established range [200,300) is
// fully disjoint from consume_count()'s required [20,100), a genuine,
// provable violation, caught purely from the constructor call's own
// established field-range fact.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct thing {
  int count;
  explicit thing (int c)
    pre<sc::proven_conveyor_v>(c >= 200 && c < 300)
    post<sc::proven_conveyor_v>(this->count >= 200 && this->count < 300)
  { count = c; }
  void consume_count ()
    pre<sc::proven_conveyor_v>(this->count >= 20 && this->count < 100)
  { }
};

int main ()
{
  thing t (250);
  t.consume_count (); // { dg-error "provably violates the precondition" }
                      // { dg-message "is established \[^\n\]*, but the precondition requires" "established fact" { target *-*-* } .-1 }
  return 0;
}
