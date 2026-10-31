// D4324: from conveyor code, a conveyor callee's precondition whose
// failed check stops execution needn't be proven true: the callee's body
// can't run with it false.  The caller owes only its evaluation's freedom
// from undefined behavior, checked where the predicate is declared.  A
// precondition that may continue past a failed check (observes) is still
// a mandatory obligation, and so is an is_object_address conjunct, which
// no run-time check can establish.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=ast" }

#include <contracts>
namespace sc = std::contracts;

struct observing_conveyor
{
  static constexpr bool is_ignored (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool constify (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool assumable (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool is_conveyor (sc::assertion_static_info) noexcept { return true; }
  constexpr void operator() (const sc::assertion_context &) const noexcept { }
};
inline constexpr observing_conveyor observing_v{};

struct V
{
  int d[4];
  int size () const conveyor { return 4; }
  int at (int i) const conveyor
    pre<sc::conveyor_assert_v> (i >= 0 && i < size ())
  { return i >= 0 && i < 4 ? d[i] : 0; }
  int at_observed (int i) const conveyor
    pre<observing_v> (i >= 0 && i < size ())
  { return i >= 0 && i < 4 ? d[i] : 0; }
};

int deref (const int *p, int n) conveyor
  pre<sc::conveyor_assert_v> (std::is_object_address (p) && n > 0)
{ return *p; }

int
terminating (const V &v, int i) conveyor
{
  return v.at (i);			// no proof of 'i < size ()' owed
}

int
terminating_bounded (const V &v, int i) conveyor
  pre<sc::conveyor_assert_v> (i >= 0 && i < 4)
{
  return v.at (i);
}

int
observed (const V &v, int i) conveyor
{
  return v.at_observed (i);	// { dg-error "satisfies the precondition" }
}

int
object_still_owed (const int *p, int n) conveyor
{
  return deref (p, n);		// { dg-error "is_object_address" }
}
