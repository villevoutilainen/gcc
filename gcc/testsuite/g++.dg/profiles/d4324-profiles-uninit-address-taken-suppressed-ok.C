// P4222/P3589: the identical shape d4324-profiles-uninit-address-
// taken-bad.C rejects, but with [[profiles::suppress(std::init)]] on
// the actual escape-causing statement -- now genuinely reachable,
// since both diagnostics this shape would otherwise produce (the
// address-escape check and the call-argument flavor-consistency
// check) are anchored at that statement, not at x's own declaration.
// Before that anchor point was fixed, no placement of suppress
// anywhere in the function could reach the address-escape diagnostic
// specifically.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void take_ptr (int *);

void f ()
{
  int x [[uninit]];
  [[profiles::suppress(std::init)]]
  take_ptr (&x);
}
