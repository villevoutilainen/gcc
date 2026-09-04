// P3589: profiles::suppress's dominion is the single declaration it's
// attached to, not an interval reaching into the rest of the enclosing
// scope -- confirms d4324-profiles-suppress-parse-ok.C's clean compile
// is genuinely due to suppress covering that one declaration, not
// suppress (once registered at all) silencing every subsequent
// diagnostic in the same function.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void f ()
{
  [[profiles::suppress(std::init)]] int x;
  int y; // { dg-error "not initialized and not marked" }
  (void) x;
  (void) y;
}
