// D4324: a contract on a class-type callable object must introduce the
// parameter names with a binder list; the names of the call operator's
// own parameters are not visible to it, at namespace scope or in a
// class.  Wrong binder counts are diagnosed in a class as well.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct adder {
  int operator() (int a, int b) const { return a + b; }
};

adder a1 pre<> (a > 0);			// { dg-error "'a' was not declared" }
adder a2 pre<> (x, y: x > 0 && y > 0);
adder a3 post<> (r: r > 0);		// a lone post name is the result

struct holder {
  adder m1 pre<> (b > 0);		// { dg-error "'b' was not declared" }
  adder m2 pre<> (x, y: x > 0);
  adder m3 pre<> (x: x > 0);		// { dg-error "wrong number of names in contract binder list" }
  void (*fp) (int, int) pre<> (x, y, z: true); // { dg-error "wrong number of names in contract binder list" }
};
