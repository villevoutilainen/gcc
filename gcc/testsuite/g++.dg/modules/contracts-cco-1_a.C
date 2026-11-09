// { dg-module-do run }
// { dg-additional-options { -std=c++26 -fmodules-ts -fcontracts -fcontract-control-objects } }
// D4324: contract control objects across a module boundary.  An inline
// function's checks, streamed with its body, call helper functions (the
// predicate's core and a thunk to it) that must be streamable with it;
// named and prvalue control objects, pre/post/contract_assert, a member
// function, and a template instantiated in the importer.

module;
#include <contracts>
export module contracts_cco_1;
// { dg-module-cmi "contracts_cco_1" }

using namespace std::contracts;

export int fails;

export struct count_t
{
  int w = 1;
  static constexpr bool
  is_ignored (assertion_static_info) noexcept { return false; }
  constexpr void
  operator() (const assertion_context& c) const
  { if (!c.check ()) fails += w; }
};
export inline constexpr count_t counter{};

export inline int f (int x) pre<counter> (x > 0) post<counter> (r: r > 1)
{ return x; }
export inline void a (int x) { contract_assert<count_t{10}> (x > 0); }
export struct S { inline int m (int x) pre<counter> (x > 0) { return x; } };
export template <class T> T t (T x) pre<count_t{100}> (x > 0) { return x; }
