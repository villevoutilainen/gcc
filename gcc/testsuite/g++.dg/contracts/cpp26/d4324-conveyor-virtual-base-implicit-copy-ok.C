// D4324: the implicitly-defined copy and assignment of a class with a
// virtual base are conveyor when every base's and member's are
// (P2680 9.2.3), and their bodies reach the virtual base through the
// C++ ABI's own pointers: the vtable pointer and the virtual base offset
// before its address point, the VTT a constructor is passed, and the
// object plus that offset.  Each is valid whenever the object (or the
// VTT) is; every one of them was rejected as an unprovable dereference.
// The source object is trusted in the body, as for any defaulted copy
// that needs it, and callers prove it.  'D b (a); a = b;' also hit the
// GIMPLE engine's resolution of a by-value argument's temporary to the
// object it was copied from, applied to the named local B: the source
// of 'a = b' was taken to be 'a' itself, not an address.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct H { int v; H (int x) conveyor : v (x) {} };
struct B1 : virtual H { B1 (int x) conveyor : H (x) {} };
struct B2 : virtual H { B2 (int x) conveyor : H (x) {} };
struct D : B1, B2 { D (int x) conveyor : H (x), B1 (x), B2 (x) {} };

int use () conveyor
{
  D a (1);
  D b (a);
  a = b;
  B1 c (2);
  B1 e (c);
  e = c;
  return a.v;
}
