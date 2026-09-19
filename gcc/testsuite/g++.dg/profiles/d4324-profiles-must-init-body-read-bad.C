// A [[must_init]] parameter's own body reading its pointee before
// writing it is flagged too, the same as a member's read-before-write
// check (ip_check_constructor_member) already is.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

int use (int);

void reads_before_write (int* p [[must_init]])
{
  use (*p); // { dg-error "read before it is definitely assigned" }
  *p = 0;
}
