// D4324: a proven_symbolic_v precondition forces symbolic analysis on by
// itself, without -fcontract-symbolic-proofs, so the fact it needs is
// established by the calls before it -- here the symbolic postcondition of
// 'open' (P4445 BUG-29, where the AST engine rejected 'f' unless the
// option was given).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=ast" }

#include <contracts>

struct io
{
  static bool is_opened (io *) symbolic;
  void open () post<std::contracts::symbolic_assert_v>(is_opened (this));
  void write () pre<std::contracts::proven_symbolic_v>(is_opened (this));
};

void f (io *p) { p->open (); p->write (); }
void g (io *p) { p->write (); } // { dg-error "cannot prove" }
