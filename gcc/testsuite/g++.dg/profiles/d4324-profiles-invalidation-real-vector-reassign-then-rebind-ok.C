// P3446R0/P4296R0 Invalidation profile: a mutation that happens
// *before* a binding is (re-)established must not count as
// invalidating that binding -- confirmed directly that this used to
// be wrongly flagged: ip_check_operand_uses (invalidation-profile-
// gimple.cc) only excluded the one call that produced the current
// binding (ip_originating_call's own self-taint exemption), never any
// OTHER earlier mutating call that happened to precede the binding's
// own establishing statement. Fixed by also requiring the mutating
// call to provably occur strictly after that establishing statement,
// not merely before the use.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

void may_invalidate (std::vector<int> &v) { v.push_back (9); }

void g ()
{
  std::vector<int> va { 1, 2 };
  std::vector<int> vb { 1, 2 };

  may_invalidate (vb);
  vb = va;
  auto p = vb.data () + 1;
  *p = 7;
}
