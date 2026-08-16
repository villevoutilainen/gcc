// D4324: analyzed_conveyor/proven_conveyor (and the symbolic mirrors)
// force analysis on for a call regardless of the command-line flag --
// but that forcing must also cover *postcondition establishment*, not
// just precondition-obligation discharge. Confirmed by direct
// testing (not assumed) that this previously, silently failed:
// oa_call_symbolic_obligation_status's own forced-computation only
// scanned PRECONDITION_P contracts, so a *postcondition* tagged
// proven_symbolic never forced the shared bookkeeping
// (oa_handle_call_symbolic_postcondition_establishment) on, leaving
// use_it's own, separately-forced precondition with nothing to prove
// even though open_it's postcondition, right above it, plainly
// establishes exactly what it needs.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

struct file { bool opened = false; };
bool is_opened (const file*) symbolic;

void open_it (file* const f) post<sc::proven_symbolic_v>(is_opened (f))
{
  f->opened = true;
}

void use_it (file* const f) pre<sc::proven_symbolic_v>(is_opened (f))
{
}

void good (file* const f)
{
  open_it (f);
  use_it (f); // proven true: is_opened(f) was just established above
}

int main ()
{
  file f;
  good (&f);
  return 0;
}
