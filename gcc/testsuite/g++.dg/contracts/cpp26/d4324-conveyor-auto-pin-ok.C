// D4324: an explicit instantiation definition repeating plain
// 'conveyor' (not 'conveyor(auto)' again) is 'conveyor(auto)''s own
// pinning mechanism -- no new syntax needed, this is exactly the same
// "explicit instantiation may repeat 'conveyor', and it must then
// match the real instantiation" rule an ordinary conveyor template's
// explicit instantiation already has, just forced to resolve a
// still-undecided specialization's answer first (maybe_instantiate_
// conveyor) instead of comparing against a not-yet-computed "false".
// A library author uses this in their own test suite to keep a
// regression in a conveyor(auto) function loud (a compile error, here
// and now) instead of latent (a confusing failure at some unrelated,
// downstream call site, possibly much later). See .claude/plans/
// lazy-stirring-pearl.md's own "Pinning and regression safety"
// section.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct Conveyor
{
  bool get () const conveyor { return true; }
};

template<typename _Tp>
bool
call_get (_Tp const& t) conveyor(auto)
{ return t.get (); }

// The pin: asserts call_get<Conveyor> must actually deduce conveyor.
template bool call_get<Conveyor> (Conveyor const&) conveyor;

int
main ()
{
  Conveyor c;
  return call_get (c) ? 0 : 1;
}
