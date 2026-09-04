// P4222 Initialization profile: std::escape_uninit() only neutralizes
// the "address taken outside a recognized call" restriction -- it does
// not, unlike now_init()/now_init_in_place(), also assert that the
// object is initialized.  A direct read of x after escape_uninit(x)
// alone, with no following now_init_in_place()/now_init(), is still
// correctly rejected.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];
#include <utility>

void write_somehow (int &r);

int use_it ()
{
  int x [[uninit]];
  write_somehow (std::escape_uninit (x));
  return x; // { dg-error "read before it is definitely assigned" }
}
