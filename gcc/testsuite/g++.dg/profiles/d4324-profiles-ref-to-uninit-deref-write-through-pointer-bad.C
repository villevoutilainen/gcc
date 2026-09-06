// P4222 Initialization profile: a WRITE through a [[ref_to_uninit]]
// pointer ('*p = 5;') does NOT count as initializing x, unlike a
// direct, by-name write to x itself (d4324-profiles-ref-to-uninit-
// deref-after-write-ok.C) -- this checker deliberately does not
// attempt general pointer-aliasing analysis for WRITES (only the
// narrow, single-hop READ case -- provably tracing straight back to
// '&x' -- is handled), so the later dereference remains unverified.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

int main ()
{
  int x [[uninit]];
  int* p [[ref_to_uninit]] = &x;
  *p = 5;
  return *p; // { dg-error "read before it is definitely assigned" }
}
