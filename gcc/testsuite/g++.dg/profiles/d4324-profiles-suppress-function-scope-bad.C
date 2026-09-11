// P3589: the opened-at-start / closed-at-finish suppression range
// d4324-profiles-suppress-function-uninit-ok.C relies on must stay
// scoped to the one function it was opened for -- it must not leak
// into a later, sibling function under the same enforced profile,
// the same guarantee d4324-profiles-suppress-scope-bad.C already
// confirms for a declaration-level suppress.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

[[profiles::suppress(std::init)]]
void f ()
{
  int x;
  (void) x;
}

void g ()
{
  int y; // { dg-error "not initialized and not marked" }
  (void) y;
}
