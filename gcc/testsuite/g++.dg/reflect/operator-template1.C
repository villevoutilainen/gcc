// P4340R1: declaring a template-argument function, operator template.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection -freflection-operator-template" }

#include <meta>
#if __GXX_REFLECTION_OPERATOR_TEMPLATE != 1
#error
#endif
struct A { int i; };
class C {
  int i;
public:
  consteval operator template() const = default;      // OK
};
struct Mod { int v; consteval operator template() const; };
template <int V> constexpr Mod canonical = {V};
consteval Mod::operator template() const {             // out-of-class definition
  return std::meta::substitute (^^canonical, {std::meta::reflect_constant ((v % 7 + 7) % 7)});
}
struct X1 { consteval operator template(this X1); };            // OK, explicit object C
struct X2 { consteval operator template(this const X2&); };     // OK
struct X3 { consteval operator template() const &; };           // OK
struct G { operator template() const; };                        // { dg-error "must be declared .consteval." }
struct H { consteval operator template(); };                    // { dg-error "object parameter of .operator template. must have type" }
struct I { consteval std::meta::info operator template() const; };  // { dg-error "return type specified for .operator template." }
struct J { consteval operator template() const; };
consteval J::operator template() const = default;               // { dg-error "can only be defaulted in its class" }
struct K : A { using A::operator template; };                    // { dg-error "cannot be named" }
consteval std::meta::info f (A a) { return a.operator template(); }   // { dg-error "cannot be named" }
constexpr std::meta::info r = ^^A::operator template;           // { dg-error "cannot be applied" }
consteval operator template();                                   // { dg-error "must be a non-static member function" }
struct L { consteval operator template(int) const; };           // { dg-error "object parameter of .operator template. must have type" }
struct M { template <class T> consteval operator template() const; };  // { dg-error "cannot be a function template" }
