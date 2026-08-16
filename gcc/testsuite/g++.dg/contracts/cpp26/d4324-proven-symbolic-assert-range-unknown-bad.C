// D4324: contract_assert<proven_symbolic_v> verifying an int is in
// range -- CANNOT PROVE. 'i' is a bare, unconstrained parameter here:
// nothing establishes or denies either half of the range. proven_
// symbolic is strict (matching WG14 P4021R2's compile_assert() own
// outcome table: "cannot prove" is a hard error, the same as "proven
// false", not silently accepted or merely a warning -- that lenient
// behavior is analyzed_symbolic's own, see d4324-analyzed-symbolic-
// unknown-ok.C). Each top-level &&-conjunct is checked and reported
// independently, hence the two separate errors below.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

int
demo_unknown (int i)
{
  contract_assert<sc::proven_symbolic_v>(i >= 0 // { dg-error "cannot prove" }
					  && i < 10); // { dg-error "cannot prove" }
  return 0;
}

int main () { return demo_unknown (5); }
