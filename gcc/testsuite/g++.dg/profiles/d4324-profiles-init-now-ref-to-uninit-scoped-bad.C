// P4222 Initialization profile: std::now_ref_to_uninit() is narrowly
// scoped to the single value it wraps -- assigning its result directly
// into an UNMARKED destination is still an error, proving the escape
// hatch doesn't disable checking wherever its result later flows, only
// bridges the one value expression it's called on.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];
[[profiles::exempt(std::init, angle_header: "cstdlib")]];
[[profiles::exempt(std::init, angle_header: "utility")]];

#include <cstdlib>
#include <utility>

void g ()
{
  void* p = std::now_ref_to_uninit (malloc (4)); // { dg-error "assigning a pointer marked \[^\n\]*ref_to_uninit\[^\n\]* into a pointer not marked" }
}
