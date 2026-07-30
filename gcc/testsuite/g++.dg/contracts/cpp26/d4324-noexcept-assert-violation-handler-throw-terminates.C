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
// __d4324_terminate is overridden locally (same idiom as
// d4324-invoke-violation-handler.C/d4324-terminate-wrapper-multi-tu.C)
// so this test doesn't need libstdc++exp linked, and so termination is
// observed via a controlled, zero exit code instead of an actual abort:
// reaching this override at all (rather than the exception escaping to
// main, or the process returning normally) is exactly what's being
// verified.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#define _GLIBCXX_ASSERTIONS
#define _GLIBCXX_ASSERTIONS_USE_VIOLATION_HANDLER
#include <contracts>
#include <cstdlib>

namespace sc = std::contracts;

bool handler_called = false;

void
handle_contract_violation (const sc::contract_violation&)
{
  handler_called = true;
  throw 42;
}

namespace std { namespace contracts {
void __d4324_log_violation (const char*, std::source_location) noexcept {}
[[noreturn]] void
__d4324_terminate () noexcept
{ std::_Exit (handler_called ? 0 : 1); }
} }

int f (int x)
{
  contract_assert<sc::noexcept_assert_v>(x >= 0);
  return x;
}

int main ()
{
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
