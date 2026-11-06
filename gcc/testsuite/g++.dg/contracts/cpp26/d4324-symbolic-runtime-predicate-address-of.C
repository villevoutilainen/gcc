// D4324: under -fcontract-symbolic-runtime-checks, a symbolic predicate
// whose argument is written '&g' is recorded and checked by g's address
// (P4445 BUG-31, where it was rejected as an unsupported conjunct).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-runtime-checks" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

int holds, violated;

struct rep
{
  static constexpr bool
  is_symbolic (std::contracts::assertion_static_info) { return true; }
  void
  operator() (const std::contracts::assertion_context &c) const
  { ++(c.check () ? holds : violated); }
};

struct io
{
  static bool is_opened (io *) symbolic;
  void open () post<rep{}>(is_opened (this)) {}
};

int main ()
{
  io g, h;
  g.open ();
  contract_assert<rep{}>(io::is_opened (&g));
  contract_assert<rep{}>(io::is_opened (&h));
  return holds >= 1 && violated == 1 ? 0 : 1;
}
