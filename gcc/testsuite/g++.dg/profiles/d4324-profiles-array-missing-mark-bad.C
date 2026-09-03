// P4222 Initialization profile, Phase 4 (arrays, S5.5): a local array
// left without an initializer must be explicitly marked [[uninit]],
// the same rule as a scalar (Phase 2).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void f ()
{
  int arr[10]; // { dg-error "not initialized and not marked" }
  (void) arr;
}
