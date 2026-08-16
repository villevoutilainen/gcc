// D4324/P2680: a genuine aggregate (no user-declared constructor)
// initialized via brace-init ('Point p = { 50.0, 2.0 };') has no
// constructor to call at all -- its own DECL_INITIAL is a plain
// CONSTRUCTOR tree, never a CALL_EXPR/AGGR_INIT_EXPR, so it was
// invisible to both the clone-contract-propagation fix and the
// AGGR_INIT_EXPR fix (both are specifically about *calling* a
// constructor). oa_walk_stmt's DECL_EXPR case now walks a CONSTRUCTOR
// initializer's own (field, value) pairs to establish field-range
// facts, the same way a direct field-write assignment already does.
// consume()'s precondition on p.x is satisfied, silently, correctly
// discharged -- note the precondition is phrased as 'p.x', a plain
// by-value object access, not 'this->x'/'ptr->x': this also needed
// oa_symbolic_comparison_conjunct_shape's own base check broadened to
// accept a non-pointer object, not just INDIRECT_REF, since a by-value
// parameter's own field access is never wrapped in one.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct Point {
  double x;
  double y;
};

void consume (Point p)
  pre<sc::proven_conveyor_v>(p.x >= 0.0 && p.x <= 100.0)
{ }

int main ()
{
  Point p = { 50.0, 2.0 };
  consume (p);
  contract_assert<sc::proven_conveyor_v>(p.x >= 0.0 && p.x <= 100.0);
  return 0;
}
