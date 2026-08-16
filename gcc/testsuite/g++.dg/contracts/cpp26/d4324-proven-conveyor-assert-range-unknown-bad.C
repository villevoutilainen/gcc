// D4324: contract_assert<proven_conveyor_v> verifying an int is in
// range -- CANNOT PROVE. Conveyor mirror of
// d4324-proven-symbolic-assert-range-unknown-bad.C: 'i' is a bare,
// unconstrained parameter, nothing establishes or denies either half
// of the range. proven_conveyor is strict (matching WG14 P4021R2's
// compile_assert() own outcome table: "cannot prove" is a hard error,
// the same as "proven false", not silently accepted or merely a
// warning -- that lenient behavior is analyzed_conveyor's own, see
// d4324-analyzed-conveyor-unknown-ok.C). Each top-level &&-conjunct is
// checked and reported independently, hence the two separate errors
// below.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

int
demo_unknown (int i)
{
  contract_assert<sc::proven_conveyor_v>(i >= 0 // { dg-error "cannot prove" }
					  && i < 10); // { dg-error "cannot prove" }
  return 0;
}

int main () { return demo_unknown (5); }
