// D4324: the relational shape ("declA OP declB", no literal on either
// side) is also checked, not just a plain scalar range -- oa_env_check_
// relational_fact_1 (the existing, reused checker) has no direct
// "established code contradicts required code" path for this shape
// (unlike its call-relational sibling's oa_call_relational_contradicts_p),
// only "does it imply" (true) or a range-vs-range fallback (false, when
// both sides independently have a known absolute range) -- so this
// exercises that fallback: a's own precondition establishes a <= 4,
// b's establishes b >= 11, so 'a > b' is genuinely, numerically
// impossible.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
namespace sc = std::contracts;

int
f (int a, int b) conveyor pre<sc::conveyor_assert_v>(a < 5 && b > 10)
{
  contract_assert<sc::conveyor_assert_v>(a > b); // { dg-error "condition .*a > b.* is provably false" }
                                                  // { dg-message "established \[^\n\]*" "established fact" { target *-*-* } .-1 }
  return 0;
}

int
main ()
{
  return f (1, 20);
}
