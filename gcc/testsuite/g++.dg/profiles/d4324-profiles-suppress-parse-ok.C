// P3589, Increment 1: profiles::suppress grammar parses (including the
// dotted profile-name), but has no semantic handling wired up yet --
// unlike profiles::enforce, it doesn't go through an empty-declaration
// (P3589 attaches it to an ordinary declaration or statement instead),
// so it isn't reachable from cp_finish_empty_declaration and still
// gets the same generic "ignored" diagnostic any other unrecognized
// attribute on a real declaration would. That's expected for this
// increment, not a bug: suppress's own semantic wiring is later work.
// { dg-do compile { target c++11 } }

void f ()
{
  [[profiles::suppress(std::init)]] int y = 0; // { dg-warning "ignored" }
  (void) y;
}
