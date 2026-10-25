// A contract_assert that is all of a loop's body is still checked: the
// check's BIND_EXPR had no TREE_SIDE_EFFECTS, and genericize_c_loop's
// append_to_statement_list dropped it.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-evaluation-semantic=observe" }

#include <contracts>

int violations;

void
handle_contract_violation (const std::contracts::contract_violation &)
{
  ++violations;
}

void f_for (int n) { for (int i = 0; i < n; ++i) contract_assert (i < 1); }
void f_braced (int n) { for (int i = 0; i < n; ++i) { contract_assert (i < 1); } }
void f_while (int n) { int i = 0; while (i++ < n) contract_assert (i < 2); }
void f_do (int n) { int i = 0; do contract_assert (i < 1); while (++i < n); }
void f_range (const int (&a)[3]) { for (int x : a) contract_assert (x < 2); }

int
main ()
{
  int a[3] = { 1, 2, 3 };
  f_for (3);
  f_braced (3);
  f_while (3);
  f_do (3);
  f_range (a);
  if (violations != 10)
    __builtin_abort ();
}
