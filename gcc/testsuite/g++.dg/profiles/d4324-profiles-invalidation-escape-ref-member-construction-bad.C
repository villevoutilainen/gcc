// P3446R0 Invalidation profile: proves the real risk the sibling
// escape-ref-member-direct-ok.C's new trust relies on -- a plain
// reference member bound to something too short-lived -- is already,
// separately caught at the point that binding actually happens, not
// merely assumed safe. 'Handle{x}' binds 'value' to a local that does
// not outlive the returned 'Handle', so this must stay flagged here,
// exactly as it did before that sibling test's trust was introduced.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

struct Handle
{
  int &value;
};

Handle make ()
{
  int x = 5;
  return Handle{x}; // { dg-error "may hold a pointer to a local" }
}
