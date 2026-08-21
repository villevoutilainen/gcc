// D4324, Increment U (defaulted-special-member half): the same
// inference must NOT fire when one member's corresponding special
// member isn't conveyor -- explicitly claiming 'conveyor' on the
// defaulted default constructor is then rejected.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct HasConveyorCtor
{
  int v;
  HasConveyorCtor () conveyor : v (0) {}
};

struct NonConveyor
{
  NonConveyor () { static int x; x = 1; }
};

struct Outer
{
  HasConveyorCtor h;
  NonConveyor n;
  Outer () conveyor = default; // { dg-error "explicitly defaulted function .Outer::Outer\\(\\). cannot be declared .conveyor. because the implicit declaration is not .conveyor." }
};
