// D4324: the GIMPLE engine checks a contract_assert's is_object_address of
// a lambda's capture through the closure's field: here the captured
// reference parameter is not provably an object.  (Resolving the call
// used to keep the capture proxy referenced in the predicate core
// function, which then failed to gimplify.)
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>
namespace sc = std::contracts;

struct C { int v; int get () const pre<sc::conveyor_assert_v> (std::is_object_address (this)) { return v; } };

int by_ref (const C& c)
{
  auto f = [&] {
    contract_assert<sc::conveyor_assert_v> (std::is_object_address (&c)); // { dg-error "cannot prove .is_object_address. for" }
    return c.get ();
  };
  return f ();
}
