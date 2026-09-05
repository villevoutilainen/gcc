// P4222 Initialization profile: assigning a [[ref_to_uninit]]-flavored
// pointer into an UNMARKED destination silently discarded its flavor
// before this check existed -- once copied into an unmarked pointer,
// nothing distinguished it from an ordinary, definitely-safe one, so
// it could go on to be passed anywhere with zero further checking.
// Covers both a cast and a same-type plain copy.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void* src [[ref_to_uninit]] = nullptr;

int main ()
{
  int* dst2 = nullptr;
  dst2 = (int*) src; // { dg-error "assigning a pointer marked \[^\n\]*ref_to_uninit\[^\n\]* into a pointer not marked" }
  void* dst3 = nullptr;
  dst3 = src; // { dg-error "assigning a pointer marked \[^\n\]*ref_to_uninit\[^\n\]* into a pointer not marked" }
}
