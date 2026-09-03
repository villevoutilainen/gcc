// P3589, Increment 1: profile-name is dotted (identifier (:: identifier)*),
// not just a single identifier -- exercise more than two components.
// Not a registered profile, so this only tests the *grammar*: parsing
// must succeed and reach semantic processing (which then correctly
// reports it as unknown), rather than failing to parse the extra ::
// component at all.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::lib::hardened)]]; // { dg-error "unknown profile" }
