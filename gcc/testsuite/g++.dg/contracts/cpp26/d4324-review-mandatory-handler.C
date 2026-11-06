// D4324: review_v and mandatory_v report a violation through the
// replaceable violation handler, review_v as observe and mandatory_v as
// enforce.  An exception the handler throws propagates out of review_v;
// mandatory_v terminates after the handler returns.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
#include <cstdlib>
#include <exception>

namespace sc = std::contracts;

int calls;
sc::evaluation_semantic last;
bool do_throw;

void
handle_contract_violation (const sc::contract_violation& v)
{
  ++calls;
  last = v.semantic ();
  if (do_throw)
    throw 1;
}

int r (int x) pre<sc::review_v>(x > 0) { return x; }
int m (int x) pre<sc::mandatory_v>(x > 0) { return x; }

[[noreturn]] void
on_terminate ()
{
  std::_Exit (calls == 3 && last == sc::evaluation_semantic::enforce ? 0 : 2);
}

int
main ()
{
  std::set_terminate (on_terminate);

  if (r (-1) != -1 || calls != 1 || last != sc::evaluation_semantic::observe)
    __builtin_abort ();

  do_throw = true;
  try
    {
      r (-1);
      __builtin_abort ();
    }
  catch (int)
    {
    }
  if (calls != 2)
    __builtin_abort ();

  do_throw = false;
  m (-1);
  __builtin_abort ();
}
