// D4324: negating an unsigned value wraps, which is defined: only a signed
// negation needs its operand proven above the type's minimum.  The AST
// scan judged '-(size_t) n' by the signed operand under the conversion.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

using size_t = decltype (sizeof 0);

bool magnitude_ok (long n) conveyor
{
  contract_assert<std::contracts::never_proven_conveyor_v>
    ((n >= 0 ? static_cast<size_t> (n) : -static_cast<size_t> (n)) <= 10);
  return true;
}

bool signed_negation (long n) conveyor
{
  contract_assert<std::contracts::never_proven_conveyor_v>
    (static_cast<size_t> (n >= 0 ? n : -n) <= 10); // { dg-error "negation of 'n' not provably free of overflow" }
  return true;
}
