// D4324: under libstdc++'s conveyor assertions, std::forward is a conveyor
// function, so the reference it returns is proven whatever its argument,
// also converted to bind a 'const' reference -- the AST engine treated
// std::forward as a pass-through and asked its argument instead.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -D_GLIBCXX_CONVEYOR_ASSERTIONS" }

#include <utility>

struct B { };
struct C { C (const B &) conveyor { } };

template<class T>
struct P
{
  T first;
  template<class U> P (U &&x) : first (std::forward<U> (x)) { }
};

P<C> f (B &&b) { return P<C> (std::move (b)); }
P<C> g (B &b) { return P<C> (b); }
