// P4222 Initialization profile: std::construct_at is recognized by
// name (decl_in_std_namespace_p + id_equal, not by any attribute on
// its own real signature -- adding [[ref_to_uninit]]/[[must_init]] to
// construct_at's actual declaration would reject nearly all ordinary,
// legitimate call sites, which pass an unflavored pointer). Passing
// '&x' directly is recognized as initializing x.
// { dg-do compile { target c++20 } }

[[profiles::enforce(std::init)]];
[[profiles::exempt(std::init, angle_header: "memory")]];

#include <memory>

int main ()
{
  int x [[uninit]];
  std::construct_at (&x, 5);
  return x;
}
