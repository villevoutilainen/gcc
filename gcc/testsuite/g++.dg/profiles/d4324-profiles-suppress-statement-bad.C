// P3589: the identical shape d4324-profiles-suppress-statement-ok.C
// accepts, minus the profiles::suppress attached to the read
// statement -- confirms that ok test's clean compile is genuinely due
// to statement-level suppress, not the checker being inert here for
// some other reason.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void f ()
{
  int x [[uninit]];
  int y = x; // { dg-error "read before it is definitely assigned" }
  (void) y;
}
