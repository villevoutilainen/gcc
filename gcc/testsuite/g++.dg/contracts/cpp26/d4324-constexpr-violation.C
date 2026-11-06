// D4324: a control type's operator() reports a contract violation found
// during constant evaluation itself, under "if consteval", with
// std::contracts::constexpr_error_str (a stand-in for P2758's
// std::constexpr_error_str, built on __builtin_constexpr_diag).  The
// compiler treats such an error like a failed bare contract with a
// terminating semantic (see check_for_failed_contracts in
// gcc/cp/constexpr.cc): later contracts are not evaluated, and the
// evaluation's result is not used, without a further "non-constant
// condition" error.  mandatory_v always terminates at runtime, so this is a
// hard error here too.  See d4324-constexpr-violation-review.C for
// review_v, which never terminates and so only warns here.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce" }

#include <contracts>

namespace sc = std::contracts;

constexpr int f (int x) pre<sc::mandatory_v>(x >= 0)
  pre<sc::mandatory_v>(x >= 5) { return x; }

// The error is reported inside <contracts> (where operator() calls
// constexpr_error_str), so it's matched by message only (line 0).  Only the
// first precondition is reported; an error for the second would be an
// excess error.
static_assert (f (-1) == -1);
// { dg-error "constexpr message: x >= 0 \\\[contract\\\]" "" { target *-*-* } 0 }
