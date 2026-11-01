// D4324: a defaulted virtual function is conveyor only if declared so.
// A base's 'virtual ~B () = default;' (or an implicitly virtual,
// implicitly-declared destructor) therefore doesn't make its overrides
// subject to the conveyor override rule: ordinary class hierarchies with
// non-conveyor members or destructors stay well-formed.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct M { ~M () {} };				// not conveyor

struct B { virtual ~B () = default; };		// not conveyor: virtual
struct D : B { M m; };				// implicit ~D, not conveyor
struct E : B { ~E () override {} };		// user-written, not conveyor
struct F : D { ~F () = default; };		// overrides a virtual: not conveyor
struct G : F { M m2; };

struct C
{
  virtual C &operator= (const C &) = default;	// not conveyor: virtual
  virtual ~C () = default;
};
struct H : C
{
  H &operator= (const C &) noexcept override { return *this; }
};

void f () { D d; E e; F f; G g; H h; h = h; }
