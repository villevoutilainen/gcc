// D4324: in a constructor's clone, the GIMPLE engine keys an assert's facts
// about a by-value class parameter ('__a._M_a', from shared_ptr_base.h's
// __shared_count) on the clone's own copy of the parameter, which its body
// uses, not on the parameter of the constructor the clone is made from.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }
#include <contracts>
template <class T> struct Al {
  constexpr Al () {}
  template <class U> constexpr Al (const Al<U>& o) pre<std::contracts::never_proven_conveyor_v> (std::is_object_address (&o)) {}
};
template <class A> struct Tag { const A& _M_a; };
struct SC {
  template <class A, class... Args> SC (int*& p, Tag<A> __a, Args&&... args)
  {
    contract_assert<std::contracts::never_proven_conveyor_v> (std::is_object_address (&__a._M_a));
    Al<int> a2 (__a._M_a);
  }
};
void f () { Al<void> a; int* p = nullptr; SC s (p, Tag<Al<void>> { a }, 1, 2); }
