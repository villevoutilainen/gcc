// D4324: under libstdc++'s conveyor assertions, std::move is a conveyor
// function with contracts.  The reference it returns is what any conveyor
// callee's returned reference is (both engines: GIMPLE recognized std::move
// as an identity call and so only tried its argument), and the call isn't
// folded into a cast, which would drop its checks.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-O2 -fcontracts -fcontract-control-objects -D_GLIBCXX_CONVEYOR_ASSERTIONS -fdump-tree-gimple" }

#include <utility>

struct A
{
  A () = default;
  A (const A &) conveyor = default;
};

struct B : A
{
  // __a, a non-conveyor constructor's parameter, isn't proven; the
  // reference std::move returns is.
  B (A &&__a) noexcept : A (std::move (__a)) { }
};

A take (A &a) { return A (std::move (a)); }
B make (A &&a) { return B (std::move (a)); }

// { dg-final { scan-tree-dump "std::move<A&> \\\(a\\\)" "gimple" } }
