// P4222 Initialization profile, Phase 4 (arrays, S4.9/S5.5): once a
// [[uninit]] array is proven initialized by a recognized [[must_init]]
// bulk-initialization call, ordinary (including computed-index)
// element access is fine -- matching the paper's own
// 'uninitialized_fill(a2,10); int x = a2[0]; // OK' example exactly.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void fill (int* p [[must_init]], int n);

void f (int i)
{
  [[uninit]] int arr[10];
  fill (arr, 10);
  int x = arr[0];
  int y = arr[i];
  arr[i] = y + x;
}
