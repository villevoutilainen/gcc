// D4324: the whole point of 'conveyor(auto)' -- an instantiation whose
// body does NOT satisfy the mandatory conveyor rules (here,
// call_get<NotConveyor>, which calls the non-conveyor NotConveyor::
// get()) must still compile and run perfectly normally from ordinary,
// non-conveyor-restricted code. No error at the template's own
// definition, and no error at this ordinary call site either -- only
// an attempt to use that specific specialization *as a conveyor
// callee* is rejected (see d4324-conveyor-auto-basic-bad.C). This is
// what makes 'conveyor(auto)' usable for a generic, widely-shared
// utility whose conveyor-ness is conditional on a template argument
// most callers don't care about at all.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct NotConveyor
{
  bool get () const { return true; }
};

template<typename _Tp>
bool
call_get (_Tp const& t) conveyor(auto)
{ return t.get (); }

int
main ()
{
  NotConveyor n;
  return call_get (n) ? 0 : 1;
}
