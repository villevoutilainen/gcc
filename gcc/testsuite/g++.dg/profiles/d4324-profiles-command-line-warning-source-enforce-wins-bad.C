// P3589: an in-source [[profiles::enforce(name)]] wins over
// -fprofiles-warning=name for the same profile -- the source's own,
// more specific declaration of intent is never weakened by a laxer
// command-line request.
// { dg-do compile { target c++11 } }
// { dg-options "-fprofiles-warning=std::init" }

[[profiles::enforce(std::init)]];

void f ()
{
  int x; // { dg-error "not initialized and not marked" }
  (void) x;
}
