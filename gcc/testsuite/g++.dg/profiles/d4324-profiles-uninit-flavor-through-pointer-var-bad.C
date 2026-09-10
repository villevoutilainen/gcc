// P4222 Initialization profile: a value flowing through a PRE-EXISTING
// declared-[[ref_to_uninit]] pointer variable (as opposed to a direct
// &E at the exact position being checked) is NOT covered by address-
// escape checking at all -- neither q=p nor other(q) below contains a
// literal &x, so ip_check_address_taken_var has nothing to say about
// either statement. Flavor-consistency remains the sole, necessary
// catch here, and its own redundant-with-escape skip (see
// ip_arg_is_direct_addr_expr_p's own comment) must NOT apply to this
// shape -- both diagnostics below must keep firing.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void other (int* q);

void f ()
{
  [[uninit]] int x;
  int* p [[ref_to_uninit]] = &x;
  int* q = p; // { dg-error "assigning a pointer marked" }
  other (q); // { dg-error "refers to \[^\n\]*memory but its parameter" }
}
