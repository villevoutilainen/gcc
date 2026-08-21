// D4324: companion negative case for d4324-conveyor-auto-basic-ok.C --
// the exact same 'conveyor(auto)' template, instantiated for a type
// whose own get() is NOT conveyor-tagged, silently deduces "not
// conveyor" for that specific specialization (no error at its own
// definition -- see d4324-conveyor-auto-ordinary-use-ok.C for
// confirmation it's still perfectly usable from ordinary code), and a
// real conveyor function trying to call it is correctly rejected, the
// same as calling any other untagged function.
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

bool user_of_not_conveyor (NotConveyor& n) conveyor // { dg-error "with non-.void. return type must contain a .return. statement" }
{ return call_get (n); } // { dg-error "not declared .conveyor." }

int
main ()
{
  NotConveyor n;
  return user_of_not_conveyor (n) ? 0 : 1;
}
