// D4324, Increment U (defaulted-special-member half): a non-deleted,
// compiler-generated special member is conveyor if every corresponding
// base/member special member is conveyor. Observed indirectly via an
// explicitly-defaulted declaration naming 'conveyor' explicitly --
// defaulted_late_check compares this against what implicitly_declare_fn
// would infer for the same signature (the same machinery already used
// for constexpr consistency), so a mismatch is a compile error while a
// match is accepted. Covers both a class whose only member has an
// explicitly-conveyor-qualified default constructor, and a class whose
// only member is of trivial type (no user-provided special members at
// all -- trivially conveyor, nothing to violate).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct HasConveyorCtor
{
  int v;
  HasConveyorCtor () conveyor : v (0) {}
};

struct WithConveyorMember
{
  HasConveyorCtor h;
  WithConveyorMember () conveyor = default;
};

struct Trivial { int v; };

struct WithTrivialMember
{
  Trivial t;
  WithTrivialMember () conveyor = default;
};

int main ()
{
  WithConveyorMember a;
  WithTrivialMember b{};
  return a.h.v + b.t.v;
}
