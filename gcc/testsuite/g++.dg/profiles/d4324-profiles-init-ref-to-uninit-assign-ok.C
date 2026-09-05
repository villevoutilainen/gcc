// P4222 Initialization profile: ip_check_call_flavor_consistency's
// existing bidirectional flavor-mismatch rule (checked only for call
// arguments before this) now also applies to a plain assignment
// between two named pointer variables -- a [[ref_to_uninit]]-flavored
// source into a [[ref_to_uninit]]-marked destination is a legitimate
// match, whether expressed as an initializer or a later assignment.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void* src [[ref_to_uninit]] = nullptr;

int main ()
{
  int* dst [[ref_to_uninit]] = (int*) src;
  dst = (int*) src;
}
