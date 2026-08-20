// D4324: the named-predicate shape ("pred_fn(decl)"/"!pred_fn(decl)")
// is also checked -- open_it's own postcondition establishes
// is_opened(f) as a fact; the contract_assert's own claim
// '!is_opened(f)' flatly contradicts it.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct file { bool opened = false; };

bool is_opened (file *f) conveyor { return f != nullptr; }

void
open_it (file * const f) post<sc::conveyor_assert_v> (is_opened (f)) // { dg-warning "cannot verify postcondition" }
{
  f->opened = true;
}

int
main ()
{
  file obj;
  file *f = &obj;
  open_it (f);
  contract_assert<sc::conveyor_assert_v>(!is_opened (f)); // { dg-error "condition .*is_opened.*f.*is provably false" }
                                                           // { dg-message "established \[^\n\]*" "established fact" { target *-*-* } .-1 }
  return 0;
}
