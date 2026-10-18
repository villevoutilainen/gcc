// The GIMPLE engine's version of d4324-conveyor-virtual-destructor.C: a
// defaulted conveyor destructor's predicate core function is built by its
// definition, not also at the end of the file from its unresolved
// condition, whose is_object_address call then failed to link.
// D4324: conveyor virtual destructors.  A defaulted virtual destructor
// is deduced conveyor whenever its base and member counterparts are,
// and the deleting destructor's call to operator delete (the work of a
// delete-expression, not of the destructor) used to violate the
// conveyor rules, so every class with 'virtual ~T () = default;' failed
// to compile under -fcontract-control-objects, even with no contracts
// at all.  Also covers a defaulted noexcept destructor that has a
// contract (the implicit 'this' precondition of a conveyor function),
// whose body has no MUST_NOT_THROW_EXPR wrapper and used to ICE.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

struct B { virtual ~B () = default; };
struct D : B { };

struct V { virtual ~V () conveyor = default; };
struct W : V { ~W () conveyor = default; };

struct H { virtual ~H () conveyor { } };

int
main ()
{
  B* b = new D;
  delete b;
  V* v = new W;
  delete v;
  H* h = new H;
  delete h;
  B local;
}
