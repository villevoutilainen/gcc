// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection" }
// A value with a subobject outside its lifetime isn't a constant
// expression, so it can't become a template parameter object through
// std::meta::reflect_constant either; we used to ICE mangling it.

#include <meta>

struct V {
  union { int a[4]; };
  int n;
  consteval V () : n (0) { }
  consteval void push (int x) { a[n++] = x; }
  consteval void pop () { using I = int; a[--n].~I (); }
};

consteval V
popped ()
{
  V v; v.push (1); v.push (2); v.push (3); v.pop ();
  return v;
}

consteval V
pushed ()
{
  V v; v.push (1); v.push (2); v.push (3); v.push (4);
  return v;
}

constexpr auto r1 = std::meta::reflect_constant (pushed ());
constexpr auto r2 = std::meta::reflect_constant (popped ()); // { dg-error "uncaught exception|reflect_constant failed" }
constexpr auto r3 = std::meta::reflect_constant_array (std::array<V, 1>{popped ()}); // { dg-error "uncaught exception|reflect_constant failed" }
