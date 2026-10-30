// D4324: a never_proven precondition claiming each element of a reference
// parameter pack is an object ('(is_object_address (&ts) && ...)') leaves
// the caller nothing to prove for the implicit precondition those
// elements get -- one conjunct per element once expanded, each of which
// used to be owed regardless (only a single-conjunct implicit
// precondition was matched against the trust).  'this' isn't trusted
// and is still owed.  An unnamed reference parameter of a template is
// held to its implicit precondition too.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=ast" }

#include <contracts>

struct Add { int operator() (int a, int b) const { return a + b; } };

struct V
{
  Add *p;
  template <class... I>
  int call (const I &... it) const
    pre<std::contracts::never_proven_conveyor_v>
      ((std::is_object_address (__builtin_addressof (it)) && ...))
  { return (*p) (*it...); }
  template <class... I>
  decltype(auto) dcall (const I &... it) const
    pre<std::contracts::never_proven_conveyor_v>
      ((std::is_object_address (__builtin_addressof (it)) && ...))
  { return (*p) (*it...); }
};

template <class T>
void h (const T &) pre<std::contracts::conveyor_assert_v> (true) {}

int
trusted (int *const &a, int *const &b)
{
  V v{};
  return v.call (a, b) + v.dcall (a, b);
}

int
untrusted_this (const V &v, int *const &a, int *const &b)
{
  return v.call (a, b)	// { dg-error "cannot prove" }
	 + v.dcall (a, b);	// { dg-error "cannot prove" }
}

void
unnamed (int &a)
{
  h (a);	// { dg-error "cannot prove" }
}
