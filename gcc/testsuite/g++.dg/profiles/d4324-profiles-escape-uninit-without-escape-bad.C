// P4222 Initialization profile: the identical shape
// d4324-profiles-escape-uninit-ok.C accepts, minus the
// std::escape_uninit() wrapper -- confirms that ok test's clean
// compile is genuinely due to escape_uninit()'s effect, not the
// checker being inert here for some other reason.  Two independent
// diagnostics fire: the address-taken check (anchored at x's own
// declaration, not at the write_somehow call that actually causes it
// -- no placement of profiles::suppress elsewhere in this function
// could work around that, confirmed separately; see escape_uninit's
// own doc comment in <utility>) and the separate, unconditional
// call-argument flavor-consistency check.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void write_somehow (int &r);

int use_it ()
{
  int x [[uninit]]; // { dg-error "its address is taken outside a recognized" }
  write_somehow (x); // { dg-error "is not marked" }
  return x;
}
