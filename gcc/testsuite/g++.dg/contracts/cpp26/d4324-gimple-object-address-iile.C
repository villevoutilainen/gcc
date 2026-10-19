// D4324: the GIMPLE engine proves the result of an immediately invoked
// lambda that returns the address of one of its captures by reference:
// it is what the caller stored for that capture, here '&a'.  Returning a
// captured pointer's value instead ('return q') proves nothing.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>
namespace sc = std::contracts;

int ok ()
{
  int a = 1;
  int* p = [&]() { return &a; }();
  contract_assert<sc::conveyor_assert_v> (std::is_object_address (p));
  return *p;
}

int bad (int* q)
{
  int* p = [&]() { return q; }();
  contract_assert<sc::conveyor_assert_v> (std::is_object_address (p)); // { dg-error "cannot prove .is_object_address. for .p." }
  return *p;
}
