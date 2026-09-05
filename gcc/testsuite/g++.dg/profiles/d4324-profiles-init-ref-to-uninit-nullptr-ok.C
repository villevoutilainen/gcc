// P4222 Initialization profile: a null pointer constant refers to no
// object at all, so it is compatible with EITHER flavor -- assigning
// or passing nullptr must never trip ip_check_assign_flavor_
// consistency/ip_check_call_flavor_consistency's own bidirectional
// mismatch rule, regardless of whether the destination/parameter is
// itself marked [[ref_to_uninit]] or not. Covers assignment,
// initialization, and call arguments, both directions.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void take_uninit (int* p [[ref_to_uninit]]);
void take_plain (int* p);

int main ()
{
  int* flavored [[ref_to_uninit]] = nullptr;
  flavored = nullptr;

  int* plain = nullptr;
  plain = nullptr;

  take_uninit (nullptr);
  take_plain (nullptr);
}
