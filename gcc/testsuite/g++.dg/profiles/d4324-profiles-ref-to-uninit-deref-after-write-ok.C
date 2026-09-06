// P4222 Initialization profile: the identical shape d4324-profiles-
// ref-to-uninit-deref-bad.C rejects, but with a direct, by-name write
// to x before the dereference -- confirms the new check is genuinely
// tracking initializedness (dominance-based DAA), not just banning
// every dereference of a [[ref_to_uninit]] pointer outright.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

int main ()
{
  int x [[uninit]];
  int* p [[ref_to_uninit]] = &x;
  x = 5;
  return *p;
}
