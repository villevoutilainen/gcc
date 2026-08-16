// D4324: proven_symbolic forces the MODIFY_EXPR ("plain assignment",
// as opposed to a declaration's own initializer) return-value
// predicate re-establishment site on too. Same shape as
// d4324-proven-symbolic-decl-expr-forces-establishment-ok.C, but 'r'
// is declared first and only later assigned from produce (), reaching
// oa_walk_stmt's MODIFY_EXPR case rather than its DECL_EXPR case --
// the two are handled by separate code paths in oa_walk_stmt, so both
// need their own coverage. No proofs flag anywhere; proven_symbolic
// alone must force this.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

bool check_it (int) symbolic;

int produce () post<sc::proven_symbolic_v>(r: check_it (r)) { return 1; }
void consume (int x) pre<sc::proven_symbolic_v>(check_it (x)) { (void) x; }

int main ()
{
  int r;
  r = produce ();
  consume (r);
  return 0;
}
