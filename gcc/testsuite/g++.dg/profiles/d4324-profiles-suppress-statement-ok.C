// P3589: profiles::suppress's dominion is granted to "a declaration or
// statement" (the paper's own wording), not declarations alone --
// attached to an ordinary statement (not a declaration), it now
// genuinely suppresses a diagnostic anchored within that statement's
// own extent, the same way it already did for a declaration.  Uses
// the "read before it is definitely assigned" diagnostic specifically
// because it's anchored at the READ statement itself, so a single
// suppressed statement is sufficient to cover it entirely -- see
// d4324-profiles-uninit-address-taken-suppressed-ok.C for the
// address-taken family's own equivalent, which used to be unreachable
// this way (always anchored at the [[uninit]] declaration instead)
// until that anchor point was fixed. Companion
// d4324-profiles-suppress-statement-bad.C is the identical shape
// without the suppress, still rejected.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void f ()
{
  int x [[uninit]];
  [[profiles::suppress(std::init)]]
  int y = x;
  (void) y;
}
