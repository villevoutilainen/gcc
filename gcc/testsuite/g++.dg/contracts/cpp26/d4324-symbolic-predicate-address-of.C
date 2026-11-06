// D4324: a symbolic predicate whose argument is written '&g' names g's
// object, as a pointer to it does: verified right after 'g.open ()'
// establishes it (P4445 BUG-31).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs" }

#include <contracts>

struct io
{
  static bool is_opened (io *) symbolic;
  void open () post<std::contracts::symbolic_assert_v>(is_opened (this)) {}
};

void f ()
{
  io g;
  g.open ();
  contract_assert<std::contracts::symbolic_assert_v>(io::is_opened (&g));
}

void h ()
{
  io g;
  contract_assert<std::contracts::symbolic_assert_v>(io::is_opened (&g)); // { dg-warning "cannot verify" }
}
