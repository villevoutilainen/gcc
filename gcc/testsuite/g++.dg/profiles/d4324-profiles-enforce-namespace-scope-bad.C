// P3589, Increment 1: profiles::enforce is only allowed at global
// scope, not inside a namespace.
// { dg-do compile { target c++11 } }

namespace foo
{
  [[profiles::enforce(std::init)]]; // { dg-error "only allowed at global scope" }
}
