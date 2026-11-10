// P4466R0 (FI-067): an exported using-declarator that names a
// declaration in its own namespace, or in an inline namespace of it,
// is looked through by a reflect-expression in an importer.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fmodules -freflection -Wno-global-module" }
// { dg-module-cmi M }

module;
namespace ns {
  inline namespace v1 {
    template <typename T> struct basic_string {};
    void f ();
    void f (int);
  }
  using string = basic_string<char>;
  struct S {};
  void g ();
  namespace detail {
    int h;
  }
}
export module M;
export namespace ns {
  using ns::basic_string;
  using ns::string;
  using ns::S;
  using ns::g;
  using ns::f;
  using detail::h;
}
