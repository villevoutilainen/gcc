// P4222 Initialization profile: the identical shape d4324-profiles-
// escape-ref-to-uninit-ok.C accepts, minus the std::escape_ref_to_
// uninit() wrapper -- confirms that ok test's clean compile is
// genuinely due to escape_ref_to_uninit()'s effect, not the checker
// being inert here for some other reason. Passing the flavored pointer
// directly to the unannotated fill_somehow is rejected by the ordinary
// call-argument flavor-consistency check (this is the exact diagnostic
// a user-reported repro hit when calling the real, unannotated
// std::uninitialized_fill this way). fill_somehow doesn't initialize
// *p (it's plain, unflavored), so the later read is flagged too, same
// as d4324-profiles-escape-uninit-without-escape-bad.C's own
// reference-typed shape.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void fill_somehow (int* p);

int use_it ()
{
  [[uninit]] int x;
  int* p [[ref_to_uninit]] = &x;
  fill_somehow (p); // { dg-error "refers to \[^\n\]*memory but its parameter" }
  return x; // { dg-error "read before it is definitely assigned" }
}
