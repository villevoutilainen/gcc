// D4324: as d4324-conveyor-defaulted-comparison-template-ok.C, with the
// defaulted comparison first synthesized inside an unevaluated operand
// (deducing its noexcept for a requirement): its contracts must still be
// substituted, or building its check crashed.
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
template <class T> concept eq = requires (const T &a) { { a == a } noexcept; };
static_assert (eq<D<long>>);
int main ()
{
  long i;
  D<long> a, b;
  a.cur = b.cur = &i;
  return !(a == b);
}
