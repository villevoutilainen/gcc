// P3446R0 Invalidation profile: a class-typed (non-pointer, non-
// reference) member initialized from a reference parameter must stay
// outside the new pointer/reference member-initialization check (see
// ip_member_init_target's own comment) entirely -- deliberately
// scoped to EXACTLY POINTER_TYPE/REFERENCE_TYPE fields, not every type
// ip_type_may_hold_pointer_p would recognize as pointer-capable.
// 'member_vec{i}' copies 'i's own VALUE into the vector (an int,
// nothing is retained), and its own construction is a GIMPLE_CALL (a
// constructor invocation), never the single-assignment shape this
// check matches -- confirmed this must stay clean, not flagged.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

struct S
{
  std::vector<int> member_vec;
  S (const int &i) : member_vec{i} {}
};
