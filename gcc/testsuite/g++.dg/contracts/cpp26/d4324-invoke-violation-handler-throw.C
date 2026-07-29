// D4324: std::contracts::invoke_violation_handler (and the
// __d4324_invoke_violation_handler intrinsic it forwards to) must not be
// noexcept: the real, control-object-less contract path already lets a
// throwing ::handle_contract_violation propagate to its caller
// unmodified (see contract-assert-run.C and friends), and a control
// object calling invoke_violation_handler must be able to do the same --
// in particular, to catch that exception itself, e.g. to log it or
// terminate with its own diagnostic. If either function were noexcept,
// the exception would be forced into std::terminate() at that function's
// own noexcept boundary, before ever reaching a try/catch in the
// control object that called it.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

namespace sc = std::contracts;

bool handler_called = false;
bool caught = false;

void
handle_contract_violation (const sc::contract_violation&)
{
  handler_called = true;
  throw 42;
}

struct catches_handler_throw {
  static constexpr bool is_ignored (sc::evaluation_semantic) { return false; }
  static constexpr bool constify (sc::evaluation_semantic) { return false; }
  static constexpr bool assumable (sc::evaluation_semantic) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  {
    if (ctx.check ())
      return;
    try
      {
        sc::invoke_violation_handler
          (ctx.kind (), ctx.semantic (), sc::detection_mode::predicate_false,
           ctx.comment (), ctx.location ());
      }
    catch (int)
      {
        caught = true;
      }
  }
};

inline constexpr catches_handler_throw catches_handler_throw_v{};

int f (int x) pre<catches_handler_throw_v>(x >= 0) { return x; }

int main ()
{
  f (-1);
  if (!handler_called || !caught)
    __builtin_abort ();
  return 0;
}
