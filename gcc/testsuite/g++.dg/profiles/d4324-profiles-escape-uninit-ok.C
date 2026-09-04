// P4222 Initialization profile: std::escape_uninit() (<utility>) lets an
// [[uninit]] object's address be passed to an arbitrary, unannotated
// function/constructor (here, an opaque write_somehow taking a plain
// int&, standing in for e.g. a not-yet-annotated standard-library type)
// without tripping the checker's blanket "address taken outside a
// recognized [[must_init]]/[[ref_to_uninit]] call" rejection.  Unlike
// now_init()/now_init_in_place(), it asserts nothing about the object's
// contents -- the object is proven initialized afterward, separately,
// via now_init_in_place(), exactly as if the escape had never happened.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];
#include <utility>

void write_somehow (int &r);

int use_it ()
{
  int x [[uninit]];
  write_somehow (std::escape_uninit (x));
  std::now_init_in_place (x);
  return x;
}
