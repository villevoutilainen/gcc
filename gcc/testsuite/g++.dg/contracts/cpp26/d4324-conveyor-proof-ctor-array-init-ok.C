// D4324/P2680: array/aggregate element initializer sibling of
// d4324-conveyor-proof-ctor-return-by-value-ok.C -- 'Number arr[2] = {
// Number(1.0), Number(2.0) };' is represented, at this pre-genericize
// stage, as build_vec_init's own compile-time-unrolled statement
// sequence wrapped in an INDIRECT_REF(NOP_EXPR(STATEMENT_LIST)) shape
// oa_walk_stmt previously had no case for at all, so the nested
// AGGR_INIT_EXPR for each element was completely unreachable, on top of
// oa_scan_calls_in_expr's own AGGR_INIT_EXPR blind spot. Both elements
// satisfy the precondition, silently, correctly discharged.
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

int main ()
{
  Number arr[2] = { Number (1.0), Number (2.0) };
  return 0;
}
