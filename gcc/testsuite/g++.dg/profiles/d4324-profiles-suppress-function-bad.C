// P3589: the identical shape d4324-profiles-suppress-function-ok.C
// accepts, minus the profiles::suppress on the function itself --
// confirms that ok test's clean compile is genuinely due to
// function-level suppress, not the checker being inert here for some
// other reason.  Also confirms an attribute in the OTHER plausible-
// looking-but-wrong position -- right before the function's own body,
// after the parameter list -- does not work either: it attaches to
// the function's TYPE, not its definition, and is silently dropped
// with an unrelated "does not apply to types" warning.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void write_somehow (int &r);

void f () [[profiles::suppress(std::init)]] // { dg-warning "does not apply to types" }
{
  int x [[uninit]]; // { dg-error "its address is taken outside a recognized" }
  write_somehow (x); // { dg-error "is not marked" }
}
