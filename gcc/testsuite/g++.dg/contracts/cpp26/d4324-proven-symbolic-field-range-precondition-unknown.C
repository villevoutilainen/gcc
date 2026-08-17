// D4324: companion to d4324-proven-symbolic-field-range-precondition-
// ok.C -- an entirely untracked field (a reference parameter with no
// established fact at all) must be diagnosed as unprovable, not
// silently accepted. proven_symbolic_v is strict, so this is a hard
// error rather than a warning, matching every other proven_* obligation
// check's own three-way outcome throughout this file.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

struct F
{
  double m_value;

  void check ()
    pre<sc::proven_symbolic_v>(this->m_value >= 0.0)
  { }
};

void
caller (F &f)
{
  f.check (); // { dg-error "cannot prove" }
}

int main () { return 0; }
