// D4324: if the replaceable ::handle_contract_violation itself throws,
// noexcept_assert's operator() must not let that exception propagate --
// it's caught and discarded, and the assertion terminates anyway (a
// throwing handler is a bug in the replaced handler, not something
// recoverable here, treated exactly like an ordinary failure). This is
// the "catch-all-then-still-terminate" pattern: unlike
// d4324-invoke-violation-handler-throw.C's catches_handler_throw (which
// deliberately continues after catching), noexcept_assert must still
// reach __d4324_terminate_wrapper afterward.
//
// Termination is observed through a terminate handler that exits with a
// controlled, zero exit code instead of aborting: reaching it at all
// (rather than the exception escaping to main, or the process returning
// normally) is exactly what's being verified.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#define _GLIBCXX_ASSERTIONS
#define _GLIBCXX_ASSERTIONS_USE_VIOLATION_HANDLER
#include <contracts>
#include <cstdlib>
#include <exception>

namespace sc = std::contracts;

bool handler_called = false;

void
handle_contract_violation (const sc::contract_violation&)
{
  handler_called = true;
  throw 42;
}

[[noreturn]] void
on_terminate ()
{ std::_Exit (handler_called ? 0 : 1); }

int f (int x)
{
  contract_assert<sc::noexcept_assert_v>(x >= 0);
  return x;
}

int main ()
{
  std::set_terminate (on_terminate);
  try
    {
      f (-1);
    }
  catch (...)
    {
      // The exception must never reach here.
      return 1;
    }
  // Nor must control return here: __d4324_terminate_wrapper (hence the
  // override above) must have been reached instead.
  return 2;
}
