// P4222 Initialization profile: 'return's merged into one exit block
// leave the merged return with no location; a flavor mismatch there
// is reported at the offending 'return', not without a location.
// { dg-do compile { target c++11 } }
// { dg-options "-O0" }

[[profiles::enforce(std::init)]];

[[ref_to_uninit]] void *raw (unsigned long);

void *
h (int id)
{
  if (id == 0)
    {
      void *p [[ref_to_uninit]] = raw (4);
      return p;	// { dg-error "returning a pointer marked" }
    }
  return nullptr;
}
