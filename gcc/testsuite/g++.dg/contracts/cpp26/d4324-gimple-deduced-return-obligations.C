// D4324: a function template with a deduced return type ('auto',
// 'decltype(auto)') is instantiated early, to deduce that type, and its
// contract conditions used to be substituted in template form then and
// never again -- a shape the oa_* engines don't read, so its
// preconditions, the synthesized is_object_address ones included,
// imposed nothing on its callers, and its own body had none of their
// facts.  Only a postcondition naming the result needs the template form.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>

template <class I>
auto t1 (const I &it) pre<std::contracts::conveyor_assert_v> (it > 0)
{ return it; }

template <class I>
decltype(auto) t2 (const I &it) conveyor
{ return it > 0; }

template <class I>
auto t3 (const I &it) pre<std::contracts::conveyor_assert_v> (it > 0)
  post<std::contracts::never_proven_conveyor_v> (r: r > 0)
{ return 1; }

template <class I>
auto sz (const I &) conveyor { return sizeof (I); }

// A precondition calling another deduced-return function template.
template <class I>
auto t4 (const I &it) pre<std::contracts::conveyor_assert_v> (sz (it) > 0)
{ return 0; }

struct S
{
  template <class I>
  auto m (const I &it) pre<std::contracts::conveyor_assert_v> (it > 0)
  { return 0; }
};

void
bad (int &a, S &s)
{
  t1 (a);	// { dg-error "cannot prove" }
  t2 (a);	// { dg-error "cannot prove" }
  t3 (a);	// { dg-error "cannot prove" }
  t4 (a);	// { dg-error "cannot prove" }
  s.m (a);	// { dg-error "cannot prove" }
}

int
good (int v)
  pre<std::contracts::conveyor_assert_v> (v > 0)
{
  S s;
  return t1 (v) + t2 (v) + t3 (v) + s.m (v);
}
