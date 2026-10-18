// D4324: shapes from libstdc++ that the GIMPLE engine used to reject while
// the AST engine accepts them:
// - an argument passed through std::forward/std::move ('_1 = std::forward
//   (e)' isn't folded away at -O0);
// - a function template instance's own precondition on a reference
//   parameter R, which reads '&*r';
// - a lambda's capture after an assert about it: each use loads it from the
//   closure anew;
// - a destructor called on 'this' in an ordinary member function, without
//   the proofs flags: like the AST engine, that obligation is checked under
//   them only;
// - std::move of a parameter's member in an ordinary constructor: reference
//   ownership (Q2) is a conveyor caller's and a predicate's rule.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>
#include <utility>

namespace sc = std::contracts;

struct E { };
template <class T> struct V {
  T t;
  constexpr V (T&& x) pre<sc::conveyor_assert_v> (std::is_object_address (&x)) : t (std::move (x)) {}
};
template <class T> constexpr auto mk (T&& e) pre<sc::never_proven_conveyor_v> (std::is_object_address (&e))
{ return V<std::decay_t<T>> (std::forward<T> (e)); }
int use_mk () { auto a = mk (E {}); return 0; }

struct N { int v; };
struct W { W (N& x) pre<sc::conveyor_assert_v> (std::is_object_address (&x)) {} };
template <class T> auto deduced (T& e) pre<sc::never_proven_conveyor_v> (std::is_object_address (&e)) { return W (e); }
void use_deduced () { N n {1}; deduced (n); }

struct C {
  int v;
  int get () const pre<sc::conveyor_assert_v> (std::is_object_address (this)) { return v; }
  int by_this () const
  {
    auto f = [this] {
      contract_assert<sc::never_proven_conveyor_v> (std::is_object_address (this));
      return get ();
    };
    return f ();
  }
};
int by_ref (const C& c)
{
  auto f = [&] {
    contract_assert<sc::never_proven_conveyor_v> (std::is_object_address (&c));
    return c.get ();
  };
  return f ();
}

struct A { };
template <class T> struct G { G (A& a, T* p) pre<sc::never_proven_conveyor_v> (std::is_object_address (&a)) : a (a), p (p) {} ~G () {} A& a; T* p; };
struct Base { virtual ~Base () {} virtual void destroy () = 0; };
template <class T> struct X final : Base {
  A al;
  ~X () noexcept { }
  void destroy () noexcept override { A a (al); G<X> g { a, this }; this->~X (); }
};
void use_x () { X<int> x; }

struct St { int* p; };
struct Holder {
  St s;
  Holder (Holder&& o) : s (std::move (o.s)) { }
};
