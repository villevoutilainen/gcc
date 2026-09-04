// P4222 Initialization profile: the identical shape
// d4324-profiles-now-init-in-place-escaped-ok.C accepts, minus the
// std::now_init_in_place() assertion -- confirms that ok test's clean
// compile is genuinely due to the assertion's effect, not the checker
// being inert here for some other reason (e.g. write_somehow's own
// [[ref_to_uninit]] parameter alone being mistaken for an initializing
// event).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void write_somehow (void *p [[ref_to_uninit]]);

int use_it ()
{
  int x [[uninit]];
  int *p [[ref_to_uninit]] = &x;
  write_somehow (p);
  return x; // { dg-error "read before it is definitely assigned" }
}
