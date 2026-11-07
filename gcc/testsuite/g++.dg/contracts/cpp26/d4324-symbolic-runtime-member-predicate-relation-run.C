// D4324: -fcontract-symbolic-runtime-checks records a relation between two
// objects, 'is_linked (o)' on a non-static member predicate: keyed by the
// object it is called on and its argument, in order.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-runtime-checks -fcontract-evaluation-semantic=observe" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
namespace sc = std::contracts;

int violations;
void handle_contract_violation (const sc::contract_violation &) { ++violations; }

struct node {
  bool is_linked (node*) const symbolic;
  void link (node* const o) post<sc::never_proven_symbolic_v>(is_linked (o)) {}
  void send (node* o) pre<sc::symbolic_assert_v>(is_linked (o)) {}
};

int
main ()
{
  node a, b, c;
  a.link (&b);
  a.send (&b);
  if (violations != 0)
    __builtin_abort ();
  a.send (&c);
  b.send (&a);
  if (violations != 2)
    __builtin_abort ();
}
