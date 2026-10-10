// P3446R0 Invalidation profile: a KNOWN, ACCEPTED limitation, not a
// regression -- recorded here so a future change doesn't silently
// "fix" or "break" this without the change being deliberate and
// re-examined.
//
// std::no_escape() correctly resolves the initializer_list argument
// itself (confirmed via gdb: ip_no_escape_call_p fires and the
// backing array's address is never examined) -- but vector's own
// initializer-list constructor ALSO takes a defaulted allocator
// argument ("const allocator_type& __a = allocator_type()"), and GCC
// materializes that default-constructed temporary and takes its
// address too, hitting the exact same "address of a local taken to
// bind a reference parameter" wall, entirely independent of the
// initializer_list/backing-array issue no_escape was built for.
//
// There is no call-site expression to wrap here: the allocator
// argument is implicit, supplied by the constructor's own default
// argument, not written by the caller. Considered and rejected two
// candidate automatic rules for this (see the conversation this test
// originates from): "the temporary's own type can't hold a pointer"
// and "the argument isn't bound to any local name" -- both have
// direct, verified counterexamples (an empty-typed Logger field still
// dangles if retained; an entirely unnamed prvalue temporary still
// dangles if its constructor retains its address instead of copying
// it) for the same underlying reason gap D itself is unresolvable:
// whether a constructor retains a reference or copies a value is a
// property of its own body, invisible to this checker either way.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];
[[profiles::exempt(std::invalidation, angle_header: "utility")]];

#include <vector>
#include <utility>

std::vector<int> f (int* x, int* y)
{
  return std::no_escape ({ *x, *y }); // { dg-error "may hold a pointer to a local" }
}
