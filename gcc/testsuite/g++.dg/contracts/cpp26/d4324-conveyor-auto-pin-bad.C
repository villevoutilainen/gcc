// D4324: companion negative case for d4324-conveyor-auto-pin-ok.C --
// pinning a specialization that does NOT actually satisfy the
// mandatory conveyor rules (NotConveyor::get() isn't conveyor-tagged)
// is a loud, immediate compile error at the explicit instantiation
// itself, exactly the regression-safety net conveyor(auto) needs: the
// specialization would otherwise silently just not be conveyor, with
// no diagnostic until some unrelated, possibly far-away caller tried
// to use it as one.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct NotConveyor
{
  bool get () const { return true; }
};

template<typename _Tp>
bool
call_get (_Tp const& t) conveyor(auto)
{ return t.get (); }

template bool call_get<NotConveyor> (NotConveyor const&) conveyor; // { dg-error ".conveyor. specified in explicit instantiation does not match" }

int
main ()
{
  NotConveyor n;
  return call_get (n) ? 0 : 1;
}
