// A reflect-expression cannot be applied to a class-scope
// using-declarator of a data member, static data member, or member type,
// whether or not the class also has a using-declarator of a member
// function (which gives it a sorted member table).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection" }

#include <meta>

struct Base {
  void f ();
  int m;
  static int s;
  struct N {};
  enum E { e };
};

struct D1 : Base {
  using Base::m;
  using Base::s;
  using Base::N;
};
constexpr std::meta::info r1 = ^^D1::m;	// { dg-error "cannot be applied to a using-declaration" }
constexpr std::meta::info r2 = ^^D1::s;	// { dg-error "cannot be applied to a using-declaration" }
constexpr std::meta::info r3 = ^^D1::N;	// { dg-error "cannot be applied to a using-declaration" }

struct D2 : Base {
  using Base::f;
  using Base::m;
  using Base::s;
  using Base::N;
  using Base::e;
  void g ();
  int own;
};
constexpr std::meta::info r4 = ^^D2::m;	// { dg-error "cannot be applied to a using-declaration" }
constexpr std::meta::info r5 = ^^D2::s;	// { dg-error "cannot be applied to a using-declaration" }
constexpr std::meta::info r6 = ^^D2::N;	// { dg-error "cannot be applied to a using-declaration" }
constexpr std::meta::info r7 = ^^D2::e;	// { dg-error "cannot be applied to a using-declaration" }

// Members found without a using-declarator are fine.
constexpr std::meta::info r8 = ^^D2::own;
constexpr std::meta::info r9 = ^^D2::g;
static_assert (^^Base::m != r8);

// Ordinary use of the using-declared members is unaffected.
int use (D2 &d) { D2::N n; (void) n; return d.m + D2::s + D2::e; }
