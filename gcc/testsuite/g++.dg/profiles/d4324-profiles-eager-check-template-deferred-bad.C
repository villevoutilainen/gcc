// P3446R0/P4222 profiles: an implicit template instantiation, whose
// own body isn't finalized (expand_or_defer_fn) until first use --
// here, deliberately after an unrelated front-end error elsewhere in
// the translation unit -- still gets eagerly checked once it is
// finalized.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

int bad_early ()
{
  return undeclared_identifier; // { dg-error "was not declared" }
}

template<class T>
int later_template ()
{
  T x [[uninit]];
  T *p [[ref_to_uninit]] = &x;
  return *p; // { dg-error "read before it is definitely assigned" }
}

int use_it ()
{
  return later_template<int> ();
}
