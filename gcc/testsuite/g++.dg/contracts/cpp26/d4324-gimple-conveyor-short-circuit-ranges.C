// D4324: in the GIMPLE engine, a range one conjunct of a single '&&'
// condition establishes ('idx < 1000') reaches the statements the
// condition guards, as it does with nested 'if's: the ranger can't see
// through the short-circuit's PHI of constants, so ranges there are
// narrowed along the edges that set its result.  Here that lets the
// contradiction with 'v.size () - idx < 5' be proven, as in the AST
// engine.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs -fcontract-analysis-engine=gimple -D_GLIBCXX_CONVEYOR_ASSERTIONS -D_GLIBCXX_PRECONDITION_ASSERTIONS" }

#include <vector>

int shifted (std::vector<int> &v, std::vector<int>::size_type idx)
  pre<std::contracts::conveyor_assert_v> (std::is_object_address (&v))
{
  if (v.size () > idx && (v.size () - idx) < 5 && idx < 1000)
    {
      idx += 15;
      return v[idx]; // { dg-error "provably violates the precondition" }
      // { dg-message "is established \[^\n\]*, but the precondition requires" "" { target *-*-* } .-1 }
    }
  return -1;
}
