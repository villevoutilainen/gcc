// D4324: invoke_violation_handler needs no dedicated exception parameter
// for a control object to hand a caught predicate exception to
// handle_contract_violation. Calling the existing, unmodified
// 5-argument invoke_violation_handler from within a catch (...) is
// enough: the compiler rewrites that call into a call to
// handle_contract_violation in place (see
// maybe_replace_d4324_violation_handler_call in gcc/cp/contracts.cc),
// so the real handler call stays dynamically nested inside that same
// catch -- an ordinary std::current_exception()/bare throw; inside the
// handler already observes it.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

namespace sc = std::contracts;

// This test never links the runtime contracts support library
// (libstdc++exp): it never uses default_v/mandatory, so no stub
// workaround is needed here.

struct payload_exception { int value; };

int expected = 42;
bool matched = false;
bool handler_called = false;

// A user-provided, replaceable violation handler -- the established
// P2900 pattern. Inspects the currently-handled exception directly,
// with no help from the contract_violation object itself.
void
handle_contract_violation (const sc::contract_violation&)
{
  handler_called = true;
  try { throw; }
  catch (const payload_exception& e) { matched = (e.value == expected); }
  catch (...) { }
}

bool
throwing_pred (int x)
{
  if (x < 0)
    throw payload_exception { expected };
  return true;
}

// A control object that catches a throwing predicate itself and calls
// invoke_violation_handler from within that same catch (...) -- no
// special exception-carrying argument needed.
struct rethrows_naturally {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  {
    try
      {
        if (ctx.check ())
          return;
        sc::invoke_violation_handler
          (ctx.kind (), ctx.semantic (), sc::detection_mode::predicate_false,
           ctx.comment (), ctx.location ());
      }
    catch (...)
      {
        sc::invoke_violation_handler
          (ctx.kind (), ctx.semantic (),
           sc::detection_mode::evaluation_exception,
           ctx.comment (), ctx.location ());
      }
  }
};

inline constexpr rethrows_naturally rethrows_naturally_v{};

int f (int x) pre<rethrows_naturally_v>(throwing_pred (x)) { return x; }

int main ()
{
  f (-1);
  if (!handler_called || !matched)
    __builtin_abort ();
  return 0;
}
