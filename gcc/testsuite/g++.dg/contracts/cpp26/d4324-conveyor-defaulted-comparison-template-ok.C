// D4324: a defaulted conveyor comparison of a class template is
// synthesized, never instantiated, so its implicit is_object_address
// preconditions must be substituted when it is synthesized: otherwise its
// body couldn't trust its own parameters, and building the precondition's
// check met the template's dependent condition and crashed.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

template <class T> struct B
{
  T *cur;
  friend bool operator== (const B &x, const B &y) noexcept conveyor
  { return x.cur == y.cur; }
};

template <class T> struct D : B<T>
{
  friend bool operator== (const D &, const D &) conveyor = default;
};

template <class T> struct M
{
  T *cur;
  bool operator== (const M &) const conveyor = default;
};

int main ()
{
  int i, j;
  D<int> a, b;
  a.cur = b.cur = &i;
  M<int> c{&i}, d{&j};
  if (!(a == b) || c == d)
    __builtin_abort ();
}
