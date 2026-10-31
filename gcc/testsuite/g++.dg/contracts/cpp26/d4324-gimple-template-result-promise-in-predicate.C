// D4324: a function template's never_proven postcondition that its
// returned reference is an object holds at a call inside an assertion's
// predicate too -- including the first call of a specialization, whose
// postcondition a predicate reads before it is substituted (still the
// pattern's 'is_object_address (__builtin_addressof (res))').
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>

struct D { int v; };
bool le (const D &, const D &) pre<std::contracts::conveyor_assert_v> (true);

struct U
{
  template <class T>
  static constexpr T &&object (T &&r)
    pre<std::contracts::never_proven_conveyor_v>
      (std::is_object_address (__builtin_addressof (r)))
    post<std::contracts::never_proven_conveyor_v>
      (res: std::is_object_address (__builtin_addressof (res)))
  { return static_cast<T &&> (r); }

  template <class T>
  static constexpr T &&plain (T &&r)
    pre<std::contracts::never_proven_conveyor_v>
      (std::is_object_address (__builtin_addressof (r)))
  { return static_cast<T &&> (r); }
};

void
f (const D &b, const D *e)
  pre<std::contracts::conveyor_assert_v> (true)
{
  contract_assert (le (b, U::object (*(e - 1))));
  contract_assert (le (b, U::object (*(e - 1))));
}

void
g (const D &b, const D *e)
  pre<std::contracts::conveyor_assert_v> (true)
{
  contract_assert (le (b, U::plain (*(e - 1))));	// { dg-error "cannot prove" }
}
