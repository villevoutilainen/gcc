// P4222 Initialization profile: the identical shape
// d4324-profiles-escape-uninit-ok.C accepts, minus the
// std::escape_uninit() wrapper -- confirms that ok test's clean
// compile is genuinely due to escape_uninit()'s effect, not the
// checker being inert here for some other reason.  Two independent
// diagnostics fire on the write_somehow call itself (both now
// anchored there, not at x's own declaration -- see
// escape_uninit's own doc comment in <utility>): the address-taken
// check and the separate, unconditional call-argument
// flavor-consistency check.  write_somehow doesn't initialize x (it's
// plain, unflavored), so the later read is flagged too: a third,
// independent diagnostic no longer hidden behind the escape check's
// own early return.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void write_somehow (int &r);

int use_it ()
{
  int x [[uninit]];
  write_somehow (x); // { dg-error "is not marked" }
  // { dg-error "call to prove it initialized" "" { target *-*-* } .-1 }
  return x; // { dg-error "read before it is definitely assigned" }
}
