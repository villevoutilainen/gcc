// Companion to d4324-profiles-invalidation-nothrow-ok.C: a placement
// new is still rejected beside the nothrow and aligned forms, and a
// pointer-holding class returned by value is still checked.
// { dg-do compile { target c++17 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "new")]];

#include <new>

void *buf ();
[[owner]] int *nothrow_new () { return new (std::nothrow) int (1); }
void placement () { new (buf ()) int (3); } // { dg-error "placement .new. not permitted" }

struct Holder { int *p; };
Holder holder (int &x) { int y = 0; Holder h{&y}; (void) x; return h; } // { dg-error "returning a pointer or container" }
