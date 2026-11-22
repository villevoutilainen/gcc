// D4324: a precondition of a class template's member may use the class's
// private and protected members (and a hidden friend's, its class's), also
// when the precondition is substituted early for a call met before the
// member's definition is instantiated (oa_maybe_instantiate_contracts) --
// also when that call is parsed tentatively, with access checks deferred to
// the caller ('(void)(a.h ())' in a lambda, as libc++'s
// TEST_LIBCPP_ASSERT_FAILURE writes it).
// Found by the plain-contracts experiment on libc++ (static_packed_bounded_iter,
// expected, common_iterator).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

struct cco
{
  static constexpr bool is_ignored (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool constify (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool assumable (sc::assertion_static_info) noexcept { return false; }
  constexpr void operator() (const sc::assertion_context &c) const noexcept
  {
    if (!c.check ())
      __builtin_abort ();
  }
};

template <class T>
class base
{
protected:
  bool has () const { return true; }
};

template <class T>
class A : base<T>
{
  int c () const { return 1; }
  int v = 2;

public:
  int f () const pre<cco{}> (c () != 0) { return 0; }
  int g () const pre (c () != 0) { return 0; }
  int h () const pre<cco{}> (this->has ()) { return 0; }
  friend int swap_ (const A &x, const A &y) pre<cco{}> (x.v == y.v) { return 0; }
};

template <class F>
int
run (F f)
{
  f ();
  return 0;
}

int
main ()
{
  A<int> a, b;
  int r = run ([&] () { (void) (a.h ()); });
  return r + a.f () + a.g () + a.h () + swap_ (a, b);
}
