// P4222 Initialization profile: a WRITE through a [[ref_to_uninit]]
// pointer ('*p = 5;') DOES count as initializing x, exactly like a
// direct, by-name write to x itself (d4324-profiles-ref-to-uninit-
// deref-after-write-ok.C) -- unlike an ARRAY_REF write (which only
// ever covers one element, never the whole array), a MEM_REF write
// through a pointer proven to equal '&x' covers the ENTIRE object,
// the same as a direct scalar write does, so there is no "partial
// coverage" reason to withhold it. Also covers a write through the
// pointer whose own RHS is itself a call's return value.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

int compute ();

int main ()
{
  int x [[uninit]];
  int* p [[ref_to_uninit]] = &x;
  *p = 5;
  int y = *p;

  int x2 [[uninit]];
  int* p2 [[ref_to_uninit]] = &x2;
  *p2 = compute ();
  int y2 = *p2;

  return y + y2;
}
