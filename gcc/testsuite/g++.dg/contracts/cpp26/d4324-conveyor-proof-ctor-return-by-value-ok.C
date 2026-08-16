// D4324/P2680: -fcontract-conveyor-proofs, a constructor call that
// materializes a temporary whose final destination isn't fixed yet
// ('return Number(50.0);') is represented as an AGGR_INIT_EXPR at the
// point oa_scan_calls_in_expr runs, previously invisible to it entirely
// (its own gate only matched CALL_EXPR) -- so the constructor's own
// precondition was never checked against the actual argument. 50.0
// satisfies the precondition, so this is silently, correctly discharged.
// Note: AGGR_INIT_EXPR_SLOT (the real receiver) is always a fresh,
// anonymous compiler temporary at this stage, never a stable identity --
// see d4324-conveyor-proof-ctor-return-by-value-no-fact.C for the
// disclosed, deliberate limitation this implies (no postcondition-based
// fact gets established for the caller in this shape).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct Number {
  double m_value;
  explicit Number (double value)
    pre<sc::proven_conveyor_v>(value >= 0.0 && value <= 100.0)
  { m_value = value; }
};

Number make () { return Number (50.0); }

int main () { make (); return 0; }
