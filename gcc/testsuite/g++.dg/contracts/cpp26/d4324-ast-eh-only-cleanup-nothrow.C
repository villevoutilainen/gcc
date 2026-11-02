// D4324: the exception-path cleanup of a constructed base or member runs
// only if what follows it in the constructor can throw; when nothing can,
// its precondition isn't checked, as the GIMPLE engine has no exception
// edge for it.  (Nor does the move from X end 'this': a constructor's
// object under construction isn't any of its parameters.)
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=ast" }

#include <contracts>
#define CA std::contracts::conveyor_assert_v
#define NP std::contracts::never_proven_conveyor_v

struct A
{
  A () conveyor {}
  A (const A &) conveyor {}
  ~A () conveyor pre<CA> (std::is_object_address (this)) {}
};
struct D { int *p; D () noexcept {} D (D &&o) noexcept : p (o.p) { o.p = nullptr; } };
struct I1 : A, D
{
  I1 (I1 &&x) noexcept pre<NP> (std::is_object_address (&x))
    : A (x), D (static_cast<D &&> (x)) {}
};
