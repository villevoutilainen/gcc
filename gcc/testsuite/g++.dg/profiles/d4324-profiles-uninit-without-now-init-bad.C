// P4222 Initialization profile: without std::now_init(), a pointer to
// an [[uninit]] local still needs its own [[ref_to_uninit]] flavor to
// read through -- confirms d4324-profiles-uninit-now-init-ok.C's
// clean compile is genuinely due to now_init()'s own effect, not the
// checker being inert here for some other reason.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

int without_now_init ()
{
  int x [[uninit]];
  int *p = &x; // { dg-error "not marked" }
  return *p;
}
