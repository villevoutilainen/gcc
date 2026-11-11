// -freflection-transparent-using-decls: a reflect-expression looks
// through every namespace-scope using-declarator; class-scope ones stay
// ill-formed, and members_of never returns a using-declarator.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection -freflection-transparent-using-decls" }

#include <meta>

struct X {};
void gf ();
int gv;
namespace A {
  void af ();
  struct AS {};
  template <typename T> struct AT {};
}
namespace N {
  using ::X;
  using ::gf;
  using ::gv;
  using A::af;
  using A::AS;
  using A::AT;
  int own;
}
static_assert (^^N::X == ^^X);
static_assert (^^N::gf == ^^gf);
static_assert (^^N::gv == ^^gv);
static_assert (^^N::af == ^^A::af);
static_assert (^^N::AS == ^^A::AS);
static_assert (^^N::AT == ^^A::AT);
static_assert (std::meta::parent_of (^^N::X) == ^^::);

// Unqualified, at global scope and inside a namespace.
using A::af;
static_assert (^^af == ^^A::af);
namespace N {
  static_assert (^^gf == ^^::gf);
}

// The P4466 cases still work.
namespace P {
  inline namespace I { void f (); }
  using I::f;
}
static_assert (^^P::f == ^^P::I::f);

// members_of doesn't return using-declarators.
consteval bool
no_using_members ()
{
  auto ms = std::meta::members_of (^^N, std::meta::access_context::unchecked ());
  return ms.size () == 1 && ms[0] == ^^N::own;
}
static_assert (no_using_members ());

// Class scope stays ill-formed.
struct Base { void f (); int m; };
struct Derived : Base {
  using Base::f;
  using Base::m;
};
constexpr std::meta::info e1 = ^^Derived::f;	// { dg-error "cannot be applied to a using-declaration" }
constexpr std::meta::info e2 = ^^Derived::m;	// { dg-error "cannot be applied to a using-declaration" }
