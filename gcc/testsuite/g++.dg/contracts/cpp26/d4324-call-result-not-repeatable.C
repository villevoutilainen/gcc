// D4324: 'if (m.toggle ())' makes m.toggle () a fact about m only if calling
// it again gives the same result.  Not so for a member of a class with a
// mutable member, which even a const member function may write (P4445
// BUG-21).  (The AST engine doesn't take facts from a branch on a call's
// result at all, so it also rejects the repeatable cases the GIMPLE
// variant of this test accepts.)
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=ast" }

#include <contracts>

namespace sc = std::contracts;

struct M
{
  mutable bool flip = false;
  bool toggle () const conveyor { flip = !flip; return flip; }
};
void need (const M &m) pre<sc::proven_conveyor_v>(m.toggle ()) {}
int a () { M m; if (m.toggle ()) need (m); return 0; } // { dg-error "cannot prove the precondition" }
