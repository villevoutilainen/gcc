// P4222 Initialization profile, Phase 4 (arrays, S1.3/S5.5): element
// access to a [[uninit]] array -- whether a read or a write -- is
// unverifiable before any recognized bulk-initialization call has
// been proven to dominate it; checked via the same dominance-based
// rule as an ordinary scalar read, not a separate "always banned"
// category (see init-profile-gimple.cc's own comment for why P4222's
// own worked example rules out a blanket ban).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void fill (int* p [[must_init]], int n);

void premature_read ()
{
  [[uninit]] int arr[10];
  int x = arr[0]; // { dg-error "read before it is definitely assigned" }
  fill (arr, 10);
}

void premature_write ()
{
  [[uninit]] int arr[10];
  arr[0] = 5; // { dg-error "read before it is definitely assigned" }
}
