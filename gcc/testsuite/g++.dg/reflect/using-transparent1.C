// P4466R0: a reflect-expression looks through a using-declarator that is
// separated from the declaration it names only by inline or unnamed
// namespaces.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection" }

#include <meta>

// The using-declarator names a declaration in its own namespace.
namespace NS1 {
  void f ();
  using NS1::f;
}
constexpr std::meta::info r1 = ^^NS1::f;
static_assert (r1 == ^^NS1::f);

// Separated by an inline namespace.
namespace NS2 {
  inline namespace Inline {
    void f ();
    struct S {};
    template <typename T> struct TT {};
    template <typename T> void tf (T);
    int v;
  }
  using Inline::f;
  using Inline::S;
  using Inline::TT;
  using Inline::tf;
  using Inline::v;
}
static_assert (^^NS2::f == ^^NS2::Inline::f);
static_assert (std::meta::parent_of (^^NS2::f) == ^^NS2::Inline);
static_assert (^^NS2::S == ^^NS2::Inline::S);
static_assert (^^NS2::TT == ^^NS2::Inline::TT);
static_assert (std::meta::is_class_template (^^NS2::TT));
static_assert (^^NS2::tf == ^^NS2::Inline::tf);
static_assert (^^NS2::v == ^^NS2::Inline::v);

// Separated by an unnamed namespace.
namespace NS3 {
  namespace {
    void f ();
  }
  using NS3::f;
}
constexpr std::meta::info r3 = ^^NS3::f;

// Several inline namespaces, on both sides.
namespace NS4 {
  inline namespace A {
    inline namespace B {
      int x;
    }
  }
  inline namespace C {
    using B::x;
  }
}
static_assert (^^NS4::x == ^^NS4::A::B::x);
static_assert (^^NS4::C::x == ^^NS4::A::B::x);

// Unqualified lookup.
namespace NS5 {
  inline namespace I {
    void g ();
  }
  using I::g;
  constexpr std::meta::info r = ^^g;
  static_assert (r == ^^I::g);
}

// The like of libstdc++'s std module.
namespace NS6 {
  inline namespace __cxx11 {
    template <typename T> struct basic_string {};
  }
  using string = basic_string<char>;
}
namespace NS6 {
  using NS6::basic_string;
  using NS6::string;
}
static_assert (^^NS6::basic_string == ^^NS6::__cxx11::basic_string);
static_assert (std::meta::template_of (std::meta::dealias (^^NS6::string))
	       == ^^NS6::basic_string);
