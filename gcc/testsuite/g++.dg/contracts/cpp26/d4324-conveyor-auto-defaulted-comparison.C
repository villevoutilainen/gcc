// D4324: a defaulted comparison declared conveyor(auto) in a class template.
// Deducing its conveyor-ness instantiates it, which for a defaulted function
// synthesizes it -- from within its own synthesis, which then defined it a
// second time with no function to define it in (an ICE).  Seen with libc++'s
// __wrap_iter::operator==.
// { dg-do run { target c++26 } }
// { dg-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

template <class T>
struct It
{
  T p;
  friend bool operator== (const It &, const It &) conveyor (auto) = default;
};

template <class T>
bool
eq (const It<T> &a, const It<T> &b) conveyor
{
  return a == b;
}

int
main ()
{
  int i = 0;
  It<int *> a{&i}, b{&i}, c{nullptr};
  if (!eq (a, b) || eq (a, c))
    __builtin_abort ();
}
