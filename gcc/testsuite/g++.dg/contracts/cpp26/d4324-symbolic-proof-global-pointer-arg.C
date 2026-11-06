// D4324: a call given a pointer read from a global variable is given a
// route to that pointer's object alone, which can't end an unrelated
// fact: 'fprintf (stderr, ...)' between 'p->open ()' and 'p->write ()'
// kept is_opened (p) in the AST engine but not in the GIMPLE engine
// (P4445 BUG-30).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs -fcontract-analysis-engine=ast" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <cstdio>

struct io
{
  static bool is_opened (io*) symbolic;
  void open () post<std::contracts::symbolic_assert_v>(is_opened (this));
  void write () pre<std::contracts::symbolic_assert_v>(is_opened (this));
};

FILE* gf;
void logf (FILE*);

void f1 (io* p) { p->open (); std::fprintf (stderr, "x\n"); p->write (); }
void f2 (io* p) { p->open (); std::fputs ("x", stderr); p->write (); }
void f3 (io* p) { p->open (); logf (gf); p->write (); }
