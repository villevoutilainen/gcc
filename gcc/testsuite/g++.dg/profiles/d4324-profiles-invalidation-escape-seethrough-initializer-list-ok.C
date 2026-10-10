// P3446R0 Invalidation profile: the original, terse initializer-list-
// of-values false positive (the very first report that started this
// whole investigation) is now fully, soundly resolved -- WITHOUT any
// std::no_escape() wrapping at all.
//
// 'return {*x, *y};' builds a compiler-synthesized backing array
// (holding the two int values) and a std::initializer_list<int>
// pointing at it, then passes both to vector's own initializer-list
// constructor -- the allocator argument (a defaulted, default-
// constructed std::allocator<int>) is ALSO passed by reference.
// Previously, the ADDR_EXPR branch in ip_escapes_locally_p treated
// "is the address-taken decl's own slot local" as unconditionally
// true for any VAR_DECL -- including the compiler-synthesized backing
// array and the compiler-synthesized allocator temporary -- flagging
// both unconditionally, regardless of what either actually held.
//
// Confirmed via gdb this is resolved PURELY via sound type-capacity
// reasoning, not via any trust/assumption about vector's own
// constructor: the backing array's own element type (int) and the
// allocator's own type (std::allocator<int>, provably stateless --
// every one of its own fields, recursively, is zero-sized) both
// structurally cannot hold a pointer at all (ip_type_may_hold_
// pointer_p), so ip_var_contents_escape_locally_p -- now reached for
// both via the ADDR_EXPR branch's own anonymous-temporary recursion,
// instead of stopping at the always-true "is this VAR_DECL's own slot
// local" check -- proves both safe immediately, without even needing
// to inspect either one's own reaching write.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

std::vector<int> f (int* x, int* y)
{
  return { *x, *y };
}
