// D4324/P2680: an immediately invoked lambda that returns a pointer it
// captures by reference returns the pointer's value at the call -- unless
// its body changes the pointer first, when the caller's facts about it
// don't hold for what it returns.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

int changed_in_body ()
{
  int a = 1;
  int* p = &a;
  int* q = [&]() { p = nullptr; return p; }();
  contract_assert<std::contracts::conveyor_assert_v>(std::is_object_address (q)); // { dg-error "cannot prove .is_object_address. for .q." }
  return *q;
}

void reset (int*& r) { r = nullptr; }

int changed_by_call (int* p)
  pre<std::contracts::conveyor_assert_v>(std::is_object_address (p))
{
  int* q = [&]() { reset (p); return p; }();
  contract_assert<std::contracts::conveyor_assert_v>(std::is_object_address (q)); // { dg-error "cannot prove .is_object_address. for .q." }
  return *q;
}

int changed_before (int* p)
  pre<std::contracts::conveyor_assert_v>(std::is_object_address (p))
{
  p = nullptr;
  int* q = [&]() { return p; }();
  contract_assert<std::contracts::conveyor_assert_v>(std::is_object_address (q)); // { dg-error "cannot prove .is_object_address. for .q." }
  return *q;
}

int unchanged ()
{
  int a = 1;
  int* p = &a;
  int* q = [&]() { return p; }();
  contract_assert<std::contracts::conveyor_assert_v>(std::is_object_address (q));
  return *q;
}
