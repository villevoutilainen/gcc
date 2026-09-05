// P4222 Initialization profile: std::now_ref_to_uninit() bridges an
// unflavored value into a flavored context -- the real, motivating
// case: implementing a [[ref_to_uninit]]-declared allocator wrapper in
// terms of the real, unannotated ::malloc, which this checker has no
// way to see is equivalent to an [[uninit]]-returning allocation.
// Without now_ref_to_uninit(), this would be flagged by both the
// builtin-LHS check (malloc's own unflavored result) and the return-
// statement check (my_malloc's own declared flavor).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];
[[profiles::exempt(std::init, angle_header: "cstdlib")]];
[[profiles::exempt(std::init, angle_header: "utility")]];

#include <cstdlib>
#include <utility>

[[ref_to_uninit]] void* my_malloc (size_t n)
{
  return std::now_ref_to_uninit (malloc (n));
}
