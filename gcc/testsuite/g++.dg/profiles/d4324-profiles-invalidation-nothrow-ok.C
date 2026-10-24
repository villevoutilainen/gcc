// P3446R0/P4296R0 Invalidation profile: std::nothrow returned from a
// function, and nothrow new.
// - A class with no pointer or reference anywhere in it can't dangle
//   when returned by value.  'std::nothrow_t' is empty, so 'return
//   std::nothrow;' leaves no trace of the copy in GIMPLE at all
//   ('return <retval>;'), which the escape check took for an
//   unrecognized, possibly dangling value.
// - 'new (std::nothrow) T' is a replaceable global allocation, not a
//   placement new: it allocates fresh storage, as a plain new does.
// { dg-do compile { target c++17 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "new")]];

#include <new>

std::nothrow_t by_value () { return std::nothrow; }
auto by_auto () { return std::nothrow; }
const std::nothrow_t &by_ref () { return std::nothrow; }
const std::nothrow_t *by_ptr () { return &std::nothrow; }
decltype(auto) by_decltype_auto () { return (std::nothrow); }

struct Empty {};
extern const Empty empty;
Empty empty_by_value () { return empty; }

struct alignas (64) Wide { int v; };

[[owner]] int *nothrow_new () { return new (std::nothrow) int (1); }
[[owner]] int *nothrow_new_from_ref () { return new (by_ref ()) int (2); }
[[owner]] Wide *aligned_nothrow_new () { return new (std::nothrow) Wide; }
