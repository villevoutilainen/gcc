// D4324: a special member function the compiler infers to be conveyor
// (9.2.3) -- D's implicit move assignment, whose only subobject operation
// is Base's vacuously conveyor one -- has implicit preconditions so that
// conveyor code calling it proves what it assumes.  Code that isn't
// conveyor never asked for the property and owes nothing: assigning a
// polymorphic class through references, as std::swap does, is fine there.
// A member explicitly declared conveyor is checked from any caller.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=ast" }

#include <contracts>

struct Base { virtual ~Base (); };
struct D : Base { };

template<class T>
void tswap (T &a, T &b)
{
  T t (static_cast<T&&> (a));
  a = static_cast<T&&> (b);
  b = static_cast<T&&> (t);
}

void plain (D &a, D &b) { D t (static_cast<D&&> (a)); a = static_cast<D&&> (b); }
void templ (D &a, D &b) { tswap (a, b); }
void conv (D &a, D *b) conveyor
{
  a = static_cast<D&&> (*b);	// { dg-error "cannot prove .is_object_address." }
}
void conv_ok (D &a, D &b) conveyor { a = static_cast<D&&> (b); }

struct E
{
  E &operator= (const E &) conveyor = default;
  virtual ~E ();
};
void explicit_conveyor (E &a, E &b)
{
  a = b;			// { dg-error "cannot prove .is_object_address." }
}
