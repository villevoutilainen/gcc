// P3589, Increment 1: minimal profiles::enforce/profiles::suppress
// grammar -- a single bare profile-name identifier, not yet given any
// semantic meaning (no dominion tracking, no real profile registry, no
// diagnosable-rule enforcement -- those are later increments). This
// test only exercises the grammar: attributes parse without a hard
// error and the identifier used as the profile name is genuinely not
// subject to name lookup (neither "made_up_profile_name" needs to
// exist as a declaration). "attribute ignored" is expected and correct
// for now -- nothing consumes these attributes semantically yet.
// { dg-do compile { target c++11 } }

[[profiles::enforce(made_up_profile_name)]]; // { dg-warning "attribute ignored" }

int x; // { dg-bogus "attribute ignored" }

void f ()
{
  [[profiles::suppress(made_up_profile_name)]] int y = 0; // { dg-warning "ignored" }
  (void) y;
}
