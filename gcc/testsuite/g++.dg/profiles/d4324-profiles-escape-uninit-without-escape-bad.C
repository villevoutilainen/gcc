// P4222 Initialization profile: the identical shape
// d4324-profiles-escape-uninit-ok.C accepts, minus the
// std::escape_uninit() wrapper -- confirms that ok test's clean
// compile is genuinely due to escape_uninit()'s effect, not the
// checker being inert here for some other reason.  Only the address-
// escape check fires on the write_somehow call itself -- the separate
// call-argument flavor-consistency check would report the identical
// fact for this exact "&x taken directly" shape, so it's skipped as
// pure duplication (see ip_arg_is_direct_addr_expr_p's own comment).
// write_somehow doesn't initialize x (it's plain, unflavored), so the
// later read is flagged too: a second, independent diagnostic no
// longer hidden behind the escape check's own early return.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void write_somehow (int &r);

int use_it ()
{
  int x [[uninit]];
  write_somehow (x); // { dg-error "before it is provably initialized" }
  return x; // { dg-error "read before it is definitely assigned" }
}
