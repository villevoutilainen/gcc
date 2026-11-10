// P4466R0: a reflect-expression still cannot look through a
// using-declarator separated from its declaration by a named
// non-inline namespace, or one at class scope.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection" }

#include <meta>

namespace NS {
  void f ();
  struct S {};
  int v;
}
using NS::f;
using NS::S;
using NS::v;
constexpr std::meta::info r1 = ^^f;	// { dg-error "cannot be applied to a using-declaration" }
constexpr std::meta::info r2 = ^^::f;	// { dg-error "cannot be applied to a using-declaration" }
constexpr std::meta::info r3 = ^^S;	// { dg-error "cannot be applied to a using-declaration" }
constexpr std::meta::info r4 = ^^v;	// { dg-error "cannot be applied to a using-declaration" }

namespace A {
  void g ();
  struct C {};
}
namespace B {
  using A::g;
  using A::C;
  constexpr std::meta::info r = ^^g;	// { dg-error "cannot be applied to a using-declaration" }
}
constexpr std::meta::info r5 = ^^B::g;	// { dg-error "cannot be applied to a using-declaration" }
constexpr std::meta::info r6 = ^^B::C;	// { dg-error "cannot be applied to a using-declaration" }

// An inline namespace on the using-declarator's side doesn't help when
// the declaration is in a named namespace.
namespace D {
  namespace E {
    int w;
  }
  inline namespace F {
    using E::w;
  }
}
constexpr std::meta::info r7 = ^^D::w;	// { dg-error "cannot be applied to a using-declaration" }

// Class scope.
struct Base {
  void f ();
};
struct Derived : Base {
  using Base::f;
};
constexpr std::meta::info r8 = ^^Derived::f;	// { dg-error "cannot be applied to a using-declaration" }
