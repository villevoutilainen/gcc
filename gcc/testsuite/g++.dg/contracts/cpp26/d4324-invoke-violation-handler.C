// D4324: a control object can invoke the same real, user-replaceable
// ::handle_contract_violation the compiler already synthesizes for a bare
// (control-object-less) contract, via the plain-named
// std::contracts::invoke_violation_handler entry point (an ordinary
// library function that forwards to the compiler-recognized
// __d4324_invoke_violation_handler) -- without needing to construct a
// contract_violation object itself (its fields are
// private, and duplicating the compiler's layout-compatible "reinterpret"
// trick in library code is neither possible nor wanted). This closes the
// last functionality gap for library-based control objects: everything a
// bare contract's built-in evaluation-semantic switch could do (call the
// handler, at whatever assertion_kind/evaluation_semantic/detection_mode
// it likes) is now reachable from ordinary control-object code too.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

// This test is compile-only with respect to the runtime contracts support
// library (libstdc++exp): it never links it. default_control/mandatory's
// operator()s call these two library-only entry points in their
// non-consteval branch (unrelated to this test, but still compiled as
// part of <contracts>, since __d4324_terminate_wrapper -- an ordinary,
// externally-linked inline function -- gets emitted into every TU that
// includes <contracts> with -fcontract-control-objects, regardless of
// whether anything in the TU actually calls it); trivial local
// definitions sidestep the resulting link error.
namespace std { namespace contracts {
void __d4324_log_violation (const char*, std::source_location) noexcept {}
[[noreturn]] void __d4324_terminate () noexcept { __builtin_trap (); }
} }

namespace sc = std::contracts;

bool handler_called = false;
sc::assertion_kind seen_kind{};
sc::evaluation_semantic seen_semantic{};
sc::detection_mode seen_mode{};
const char* seen_comment = nullptr;

// A user-provided, replaceable violation handler -- the established
// P2900 pattern (see contract-assert-run.C). Since this test declares its
// own overload directly, the compiler finds and uses it without needing
// libstdc++exp's weak default at all.
void
handle_contract_violation (const sc::contract_violation& v)
{
  handler_called = true;
  seen_kind = v.kind ();
  seen_semantic = v.semantic ();
  seen_mode = v.mode ();
  seen_comment = v.comment ();
}

// A custom control object whose operator(), on a failing check(), invokes
// the real handler directly via invoke_violation_handler -- passing its
// own choice of evaluation_semantic (enforce here), independent of the
// TU's own -fcontract-evaluation-semantic= (which isn't even set on this
// test, so defaults to enforce anyway -- the point is that the control
// object decides this, not the compiler). Deliberately does nothing else
// afterward: no termination, matching invoke_violation_handler's own
// contract that it only invokes the handler and leaves everything else
// to the caller.
struct calls_handler {
  static constexpr bool is_ignored (sc::evaluation_semantic) { return false; }
  static constexpr bool constify = false;
  static constexpr bool assumable = false;
  void
  operator() (const sc::assertion_context& ctx) const
  {
    if (ctx.check ())
      return;
    sc::invoke_violation_handler
      (ctx.kind (), sc::evaluation_semantic::enforce,
       sc::detection_mode::predicate_false, ctx.comment (), ctx.location ());
  }
};

inline constexpr calls_handler calls_handler_v{};

int f (int x) pre<calls_handler_v>(x >= 0) { return x; }

int main ()
{
  f (-1);
  if (!handler_called)
    __builtin_abort ();
  if (seen_kind != sc::assertion_kind::pre)
    __builtin_abort ();
  if (seen_semantic != sc::evaluation_semantic::enforce)
    __builtin_abort ();
  if (seen_mode != sc::detection_mode::predicate_false)
    __builtin_abort ();
  if (!seen_comment || __builtin_strcmp (seen_comment, "x >= 0") != 0)
    __builtin_abort ();
  return 0;
}
