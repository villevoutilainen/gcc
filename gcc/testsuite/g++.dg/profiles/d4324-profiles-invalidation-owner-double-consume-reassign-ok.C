// P3446R0/P4296R0 Invalidation profile: consuming a FRESH value after
// a reassignment must NOT be mistaken for double-consuming the
// original one -- 'delete p;' below fully accounts for the ORIGINAL
// parameter value, 'p = g ();' hands p a genuinely NEW owned value
// (already checked, independently, by its own leak-point-2 obligation
// not to be reassigned again while unconsumed), and the second
// 'delete p;' consumes THAT new value, not the original.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

[[owner]] int* g ();

void caller ([[owner]] int *p)
{
  delete p;
  p = g ();
  delete p;
}
