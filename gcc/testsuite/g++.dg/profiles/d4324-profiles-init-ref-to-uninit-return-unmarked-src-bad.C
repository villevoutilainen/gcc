// P4222 Initialization profile: the reverse direction of the previous
// test -- assigning an ORDINARY (unflavored) function's return value
// into a [[ref_to_uninit]]-marked destination is equally an error.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void* ordinary_returning_fn ();

int main ()
{
  void* p [[ref_to_uninit]] = ordinary_returning_fn (); // { dg-error "assigning a pointer not marked \[^\n\]*ref_to_uninit\[^\n\]* into a pointer marked" }
}
