// P4222 Initialization profile: std::construct_at(p, ...) where p is a
// [[ref_to_uninit]] pointer PROVABLY tracing back to '&x' (not a
// literal '&x' argument) is also recognized as initializing x --
// deliberately without touching p's own flavor at all: only the
// RETURN value of construct_at is meant to be trusted afterward
// (matching now_init's own by-value pass-through design), but the
// argument itself is still recognized as an initializing event for
// whatever it points to. Covers both a direct read of x afterward and
// a dereference of p itself (the deref-read fix from the same file).
// { dg-do compile { target c++20 } }

[[profiles::enforce(std::init)]];
[[profiles::exempt(std::init, angle_header: "memory")]];

#include <memory>

int main ()
{
  int x [[uninit]];
  int* p [[ref_to_uninit]] = &x;
  std::construct_at (p, 5);
  int a = x;
  int b = *p;
  return a + b;
}
